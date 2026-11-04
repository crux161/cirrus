//! CIRRUS build/staging tasks.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

const SMOKE_MARKER: &str = "CIRRUS C smoke: svc #0 trapped and returned";
const DEFAULT_TIMEOUT_SECS: u64 = 90;

fn main() {
    if let Err(err) = run() {
        eprintln!("xtask: {err}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), String> {
    let mut args = env::args().skip(1);
    let command = args.next().unwrap_or_else(|| "help".to_string());
    let rest: Vec<String> = args.collect();
    match command.as_str() {
        "qemu-smoke" => qemu_smoke(&rest),
        "help" | "-h" | "--help" => {
            print_help();
            Ok(())
        }
        other => Err(format!(
            "unknown xtask command '{other}'; try `cargo xtask help`"
        )),
    }
}

fn print_help() {
    println!(
        "cargo xtask qemu-smoke [--arch aarch64] [--timeout SECONDS] [--no-run]\n\
         \n\
         Builds the kernel, niji-uefi, and the C smoke; stages an ESP with the\n\
         smoke as bin/sora; boots QEMU/EDK2; waits for the smoke marker."
    );
}

fn qemu_smoke(args: &[String]) -> Result<(), String> {
    let mut arch = String::from("aarch64");
    let mut timeout = DEFAULT_TIMEOUT_SECS;
    let mut run_qemu = true;
    let mut qemu_override: Option<String> = None;
    let mut firmware_override: Option<String> = None;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--arch" => {
                i += 1;
                arch = args.get(i).cloned().ok_or("--arch needs a value")?;
            }
            "--timeout" => {
                i += 1;
                timeout = args
                    .get(i)
                    .and_then(|v| v.parse::<u64>().ok())
                    .ok_or("--timeout needs seconds")?;
            }
            "--no-run" => run_qemu = false,
            "--qemu" => {
                i += 1;
                qemu_override = Some(args.get(i).cloned().ok_or("--qemu needs a path")?);
            }
            "--firmware" => {
                i += 1;
                firmware_override = Some(args.get(i).cloned().ok_or("--firmware needs a path")?);
            }
            other => return Err(format!("unknown qemu-smoke option '{other}'")),
        }
        i += 1;
    }

    if arch != "aarch64" {
        return Err(format!(
            "unsupported arch '{arch}'; only aarch64 is wired in this slice"
        ));
    }

    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .ok_or("xtask has no parent directory")?
        .to_path_buf();
    let build = root.join("build/qemu-smoke");
    fs::create_dir_all(&build).map_err(|err| format!("create {}: {err}", build.display()))?;

    run_cargo(
        &root,
        &[
            "build",
            "-p",
            "kernel",
            "--target",
            "aarch64-unknown-none",
            "--release",
        ],
    )?;
    run_cargo(
        &root,
        &[
            "build",
            "-p",
            "niji-uefi",
            "--target",
            "aarch64-unknown-uefi",
            "--release",
        ],
    )?;
    build_c_smoke(&root, &build)?;

    let kernel = root.join("target/aarch64-unknown-none/release/kumo-kernel");
    let loader = root.join("target/aarch64-unknown-uefi/release/niji-uefi.efi");
    if !kernel.is_file() {
        return Err(format!("kernel ELF missing: {}", kernel.display()));
    }
    if !loader.is_file() {
        return Err(format!("niji-uefi missing: {}", loader.display()));
    }

    let smoke = build.join("c_smoke.elf");
    let smoke_bytes =
        fs::read(&smoke).map_err(|err| format!("read {}: {err}", smoke.display()))?;
    let initrd = build_initrd(&[("bin/sora", smoke_bytes.as_slice())])?;
    let initrd_path = build.join("initrd.img");
    fs::write(&initrd_path, &initrd)
        .map_err(|err| format!("write {}: {err}", initrd_path.display()))?;

    let esp = build.join("esp");
    if esp.exists() {
        fs::remove_dir_all(&esp).map_err(|err| format!("clean {}: {err}", esp.display()))?;
    }
    fs::create_dir_all(esp.join("EFI/BOOT"))
        .map_err(|err| format!("create ESP boot dir: {err}"))?;
    fs::create_dir_all(esp.join("EFI/KUMO/kernel"))
        .map_err(|err| format!("create ESP KUMO dir: {err}"))?;
    fs::copy(&loader, esp.join("EFI/BOOT/BOOTAA64.EFI"))
        .map_err(|err| format!("stage BOOTAA64.EFI: {err}"))?;
    fs::copy(&kernel, esp.join("EFI/KUMO/kernel/kumo-kernel.elf"))
        .map_err(|err| format!("stage kernel: {err}"))?;
    fs::copy(&initrd_path, esp.join("EFI/KUMO/initrd.img"))
        .map_err(|err| format!("stage initrd: {err}"))?;
    let manifest = "kernel = \\EFI\\KUMO\\kernel\\kumo-kernel.elf\n\
                    initrd = \\EFI\\KUMO\\initrd.img\n\
                    dtb = firmware\n\
                    board = qemu-virt-aarch64\n";
    fs::write(esp.join("EFI/KUMO/nijigumo.conf"), manifest)
        .map_err(|err| format!("stage boot manifest: {err}"))?;

    println!("kernel: {}", kernel.display());
    println!("loader: {}", loader.display());
    println!("smoke:  {}", smoke.display());
    println!("initrd: {} ({} bytes)", initrd_path.display(), initrd.len());
    println!("ESP:    {}", esp.display());

    if !run_qemu {
        return Ok(());
    }

    let qemu = qemu_override
        .or_else(|| env::var("QEMU").ok())
        .unwrap_or_else(|| String::from("qemu-system-aarch64"));
    let firmware = firmware_override
        .or_else(|| env::var("QEMU_EFI").ok())
        .map(PathBuf::from)
        .or_else(find_firmware)
        .ok_or("no aarch64 EDK2 firmware found; pass --firmware or set QEMU_EFI")?;

    let log = build.join("serial.log");
    run_qemu_and_wait(&qemu, &firmware, &esp, &log, timeout)
}

fn run_cargo(root: &Path, args: &[&str]) -> Result<(), String> {
    let cargo = env::var("CARGO").unwrap_or_else(|_| String::from("cargo"));
    let status = Command::new(cargo)
        .args(args)
        .current_dir(root)
        .env("KUMO_CONSOLE_LOGLEVEL", "7")
        .status()
        .map_err(|err| format!("spawn cargo {}: {err}", args.join(" ")))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("cargo {} failed with {status}", args.join(" ")))
    }
}

fn run_command(command: &mut Command, what: &str) -> Result<(), String> {
    let status = command
        .status()
        .map_err(|err| format!("spawn {what}: {err}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("{what} failed with {status}"))
    }
}

fn build_c_smoke(root: &Path, out: &Path) -> Result<(), String> {
    let clang = env::var("CLANG").unwrap_or_else(|_| String::from("clang"));
    let lld = env::var("LD_LLD").unwrap_or_else(|_| String::from("ld.lld"));
    let include = root.join("include");

    let objects = [
        ("userland/crt0.S", "crt0.o"),
        ("userland/runtime.c", "runtime.o"),
        ("tests/c_smoke.c", "c_smoke.o"),
    ];
    for (source, object) in objects {
        let mut command = Command::new(&clang);
        command
            .arg("--target=aarch64-none-elf")
            .arg("-ffreestanding")
            .arg("-fno-stack-protector")
            .arg("-fno-pic")
            .arg("-fno-builtin")
            .arg("-O2")
            .arg("-I")
            .arg(&include)
            .arg("-c")
            .arg(root.join(source))
            .arg("-o")
            .arg(out.join(object));
        run_command(&mut command, "clang")?;
    }

    let mut command = Command::new(&lld);
    command
        .arg("-T")
        .arg(root.join("userland/user.ld"))
        .arg("-static")
        .arg("-nostdlib")
        .arg("-e")
        .arg("_start")
        .arg("-o")
        .arg(out.join("c_smoke.elf"))
        .arg(out.join("crt0.o"))
        .arg(out.join("runtime.o"))
        .arg(out.join("c_smoke.o"));
    run_command(&mut command, "ld.lld")
}

fn build_initrd(files: &[(&str, &[u8])]) -> Result<Vec<u8>, String> {
    const HEADER_LEN: usize = 16;
    const ENTRY_LEN: usize = 80;
    const PATH_MAX: usize = 64;
    const VERSION: u32 = 1;

    let table_len = HEADER_LEN
        .checked_add(ENTRY_LEN * files.len())
        .ok_or("initrd table too large")?;
    let mut initrd = vec![0u8; table_len];
    initrd[..8].copy_from_slice(b"KUMORD01");
    initrd[8..12].copy_from_slice(&VERSION.to_le_bytes());
    initrd[12..16].copy_from_slice(&(files.len() as u32).to_le_bytes());

    let mut cursor = table_len as u64;
    for (index, (path, bytes)) in files.iter().enumerate() {
        if path.is_empty() || path.len() >= PATH_MAX {
            return Err(format!("initrd path '{path}' does not fit"));
        }
        let base = HEADER_LEN + index * ENTRY_LEN;
        initrd[base..base + path.len()].copy_from_slice(path.as_bytes());
        initrd[base + PATH_MAX..base + PATH_MAX + 8].copy_from_slice(&cursor.to_le_bytes());
        initrd[base + PATH_MAX + 8..base + PATH_MAX + 16]
            .copy_from_slice(&(bytes.len() as u64).to_le_bytes());
        initrd.extend_from_slice(bytes);
        cursor = initrd.len() as u64;
    }
    Ok(initrd)
}

fn find_firmware() -> Option<PathBuf> {
    const CANDIDATES: &[&str] = &[
        "/opt/homebrew/share/qemu/edk2-aarch64-code.fd",
        "/usr/local/share/qemu/edk2-aarch64-code.fd",
        "/usr/share/qemu/edk2-aarch64-code.fd",
        "/usr/share/AAVMF/AAVMF_CODE.fd",
        "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
    ];
    CANDIDATES
        .iter()
        .map(PathBuf::from)
        .find(|path| path.is_file())
}

fn run_qemu_and_wait(
    qemu: &str,
    firmware: &Path,
    esp: &Path,
    log: &Path,
    timeout_secs: u64,
) -> Result<(), String> {
    let _ = fs::remove_file(log);
    let drive = format!("file=fat:rw:{},format=raw,if=virtio", esp.display());

    let mut child = Command::new(qemu)
        .arg("-M")
        .arg("virt,gic-version=3")
        .arg("-cpu")
        .arg("cortex-a72")
        .arg("-m")
        .arg("1024")
        .arg("-display")
        .arg("none")
        .arg("-monitor")
        .arg("none")
        .arg("-serial")
        .arg(format!("file:{}", log.display()))
        .arg("-bios")
        .arg(firmware)
        .arg("-drive")
        .arg(&drive)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::inherit())
        .spawn()
        .map_err(|err| format!("spawn {qemu}: {err}"))?;

    let deadline = Instant::now() + Duration::from_secs(timeout_secs);
    let mut smoke_seen = false;
    let mut userland_seen = false;

    loop {
        if Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            print_serial(log);
            return Err(format!("qemu-smoke timed out after {timeout_secs}s"));
        }

        if let Ok(bytes) = fs::read(log) {
            let text = String::from_utf8_lossy(&bytes);

            if !smoke_seen && text.contains(SMOKE_MARKER) {
                smoke_seen = true;
                println!("qemu-smoke: C marker observed; waiting for post-exit USERLAND check");
            }

            if let Some(line) = text.lines().find(|line| line.contains("USERLAND  EL0")) {
                if line.contains("FAIL") {
                    let _ = child.kill();
                    let _ = child.wait();
                    print_serial(log);
                    return Err(format!("kernel reported a failed userland check: {line}"));
                }
                userland_seen = true;
            }

            if smoke_seen && userland_seen {
                println!("qemu-smoke: ProcessExit handled; kernel continued to the USERLAND check");
                let _ = child.kill();
                let _ = child.wait();
                print_serial(log);
                return Ok(());
            }

            for failure in [
                "handoff      : INVALID",
                "returning to firmware",
                "Kernel panic",
                "panicked at",
                "TOWER",
            ] {
                if text.contains(failure) {
                    let _ = child.kill();
                    let _ = child.wait();
                    print_serial(log);
                    return Err(format!("qemu-smoke failed: transcript contains '{failure}'"));
                }
            }
        }

        if let Some(status) = child
            .try_wait()
            .map_err(|err| format!("wait for qemu: {err}"))?
        {
            print_serial(log);
            return Err(format!("QEMU exited before the smoke completed ({status})"));
        }

        thread::sleep(Duration::from_millis(100));
    }
}

fn print_serial(log: &Path) {
    match fs::read(log) {
        Ok(bytes) => {
            println!("--- qemu serial ---");
            print!("{}", String::from_utf8_lossy(&bytes));
            println!("\n--- end qemu serial ---");
        }
        Err(err) => eprintln!("could not read {}: {err}", log.display()),
    }
}
