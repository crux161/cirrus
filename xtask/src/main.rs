//! CIRRUS build/staging tasks.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use kumo_bsp::{Board, Console};

const SMOKE_MARKER: &str = "CIRRUS C smoke: svc #0 trapped and returned";
const RING_MARKER: &str = "CIRRUS C IPC ring: spsc ok";
const VMO_IPC_MARKER: &str = "CIRRUS C VMO IPC: cross-process ok";
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
        "qemu-loader" => qemu_loader(&rest),
        "bootimg" => bootimg(&rest),
        "stage-esp" => stage_esp(&rest),
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
         smoke as bin/sora; boots QEMU/EDK2; waits for the smoke marker.\n\
         \n\
         cargo xtask qemu-loader [--timeout SECONDS] [--no-run] [--stdio] [--qemu PATH]\n\
         Builds the CIRRUS loader and the real KUMO kernel, packs the kernel ELF\n\
         and bin/sora into a KUMORD01 ramdisk, and boots the pair under QEMU virt\n\
         with the loader as the entry point. --stdio attaches the serial console\n\
         to the terminal for interactive use; otherwise the run is checked for\n\
         the loader, smoke, IPC ring and USERLAND markers.\n\
         \n\
         cargo xtask bootimg [--slot a|b] [--profile oneplus-enchilada|oneplus-fajita]\n\
         \t[--dtb PATH] [--header-version 0|1|2] [--page-size N] [--cmdline STR]\n\
         \t[--no-gzip] [--no-append-dtb] [--out PATH]\n\
         Builds the loader flat binary and the KUMO kernel/initrd ramdisk and\n\
         packs an Android boot image for fastboot boot / a slot write.\n\
         \n\
         cargo xtask stage-esp [--board ID] [--out DIR]\n\
         Builds the kernel, niji-uefi and the C smoke and stages a UEFI ESP tree\n\
         with board = ID. Use for UEFI targets such as orange-pi-5-plus; QEMU\n\
         itself cannot emulate RK3588."
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
    let smoke_bytes = fs::read(&smoke).map_err(|err| format!("read {}: {err}", smoke.display()))?;
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

const SMOKE_SMOKE: &str = SMOKE_MARKER;
const LOADER_ENTRY_MARKER: &str = "CIRRUS loader: entry";
const LOADER_HIGH_MARKER: &str = "CIRRUS loader: entering high half";
const LOADER_TEST_BASE: u64 = 0x4008_0000;
const ONEPLUS_LOADER_BASE: u64 = 0x8000_8000;
const LOADER_DTB_ADDR: u64 = 0x4700_0000;
const LOADER_INITRD_ADDR: u64 = 0x5000_0000;

fn loader_qemu_command(qemu: &str, loader: &Path, dtb: &Path, initrd: &Path) -> Command {
    let mut command = Command::new(qemu);
    command
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
        .arg("-no-reboot")
        .arg("-kernel")
        .arg(loader)
        .arg("-device")
        .arg(format!(
            "loader,file={},addr=0x{LOADER_DTB_ADDR:x},force-raw=on",
            dtb.display()
        ))
        .arg("-device")
        .arg(format!(
            "loader,file={},addr=0x{LOADER_INITRD_ADDR:x},force-raw=on",
            initrd.display()
        ));
    command
}

fn qemu_loader(args: &[String]) -> Result<(), String> {
    let mut timeout = DEFAULT_TIMEOUT_SECS;
    let mut run_qemu = true;
    let mut stdio = false;
    let mut qemu_override: Option<String> = None;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--timeout" => {
                i += 1;
                timeout = args
                    .get(i)
                    .and_then(|value| value.parse::<u64>().ok())
                    .ok_or("--timeout needs seconds")?;
            }
            "--no-run" => run_qemu = false,
            "--stdio" => stdio = true,
            "--qemu" => {
                i += 1;
                qemu_override = Some(args.get(i).cloned().ok_or("--qemu needs a path")?);
            }
            other => return Err(format!("unknown qemu-loader option '{other}'")),
        }
        i += 1;
    }

    if stdio && !run_qemu {
        return Err("--stdio and --no-run are mutually exclusive".to_string());
    }

    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .ok_or("xtask has no parent directory")?
        .to_path_buf();
    let build = root.join("build/qemu-loader");
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
    build_c_smoke(&root, &build)?;
    let loader = build_loader(&root, &build, LOADER_TEST_BASE, true)?;

    let kernel = root.join("target/aarch64-unknown-none/release/kumo-kernel");
    let smoke = build.join("c_smoke.elf");
    let kernel_bytes =
        fs::read(&kernel).map_err(|err| format!("read {}: {err}", kernel.display()))?;
    let smoke_bytes = fs::read(&smoke).map_err(|err| format!("read {}: {err}", smoke.display()))?;
    let initrd = build_initrd(&[
        ("boot/kumo-kernel.elf", kernel_bytes.as_slice()),
        ("bin/sora", smoke_bytes.as_slice()),
    ])?;
    let initrd_path = build.join("initrd.img");
    fs::write(&initrd_path, &initrd)
        .map_err(|err| format!("write {}: {err}", initrd_path.display()))?;

    let qemu = qemu_override
        .or_else(|| env::var("QEMU").ok())
        .unwrap_or_else(|| String::from("qemu-system-aarch64"));
    let dtb = build.join("qemu.dtb");
    dump_qemu_dtb(&qemu, &dtb)?;
    patch_initrd_chosen(&dtb, LOADER_INITRD_ADDR, initrd.len() as u64)?;

    println!("loader: {}", loader.display());
    println!("kernel: {}", kernel.display());
    println!("initrd: {} ({} bytes)", initrd_path.display(), initrd.len());
    println!("dtb:    {}", dtb.display());

    if !run_qemu {
        return Ok(());
    }

    if stdio {
        println!("qemu-loader: serial attached to stdio; Ctrl-C exits QEMU");
        let mut command = loader_qemu_command(&qemu, &loader, &dtb, &initrd_path);
        command
            .arg("-serial")
            .arg("stdio")
            .stdin(Stdio::inherit())
            .stdout(Stdio::inherit())
            .stderr(Stdio::inherit());
        let status = command
            .status()
            .map_err(|err| format!("spawn {qemu}: {err}"))?;
        println!("qemu-loader: QEMU exited ({status})");
        return Ok(());
    }

    let log = build.join("serial.log");
    let _ = fs::remove_file(&log);
    let mut child = loader_qemu_command(&qemu, &loader, &dtb, &initrd_path)
        .arg("-serial")
        .arg(format!("file:{}", log.display()))
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::inherit())
        .spawn()
        .map_err(|err| format!("spawn {qemu}: {err}"))?;

    let deadline = Instant::now() + Duration::from_secs(timeout);
    let mut loader_seen = false;
    let mut high_seen = false;
    let mut smoke_seen = false;
    let mut ring_seen = false;
    let mut vmo_seen = false;
    let mut userland_seen = false;

    loop {
        if Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            print_serial(&log);
            return Err(format!("qemu-loader timed out after {timeout}s"));
        }
        if let Ok(bytes) = fs::read(&log) {
            let text = String::from_utf8_lossy(&bytes);
            if !loader_seen && text.contains(LOADER_ENTRY_MARKER) {
                loader_seen = true;
                println!("qemu-loader: loader banner observed");
            }
            if !high_seen && text.contains(LOADER_HIGH_MARKER) {
                high_seen = true;
                println!("qemu-loader: loader entered the high half");
            }
            if !smoke_seen && text.contains(SMOKE_SMOKE) {
                smoke_seen = true;
                println!("qemu-loader: C marker observed");
            }
            if !ring_seen && text.contains(RING_MARKER) {
                ring_seen = true;
                println!("qemu-loader: C IPC ring marker observed");
            }
            if !vmo_seen && text.contains(VMO_IPC_MARKER) {
                vmo_seen = true;
                println!("qemu-loader: C VMO IPC marker observed");
            }
            if let Some(line) = text.lines().find(|line| line.contains("USERLAND  EL0")) {
                if line.contains("FAIL") {
                    let _ = child.kill();
                    let _ = child.wait();
                    print_serial(&log);
                    return Err(format!("kernel reported a failed userland check: {line}"));
                }
                userland_seen = true;
            }
            if text.contains("CIRRUS loader: FAIL") {
                let _ = child.kill();
                let _ = child.wait();
                print_serial(&log);
                return Err("cirrus-boot reported a loader failure".to_string());
            }
            for failure in [
                "handoff      : INVALID",
                "returning to firmware",
                "Kernel panic",
                "panicked at",
                "TOWER",
                "CIRRUS C VMO IPC: FAIL",
            ] {
                if text.contains(failure) {
                    let _ = child.kill();
                    let _ = child.wait();
                    print_serial(&log);
                    return Err(format!(
                        "qemu-loader failed: transcript contains '{failure}'"
                    ));
                }
            }
            if loader_seen && high_seen && smoke_seen && ring_seen && vmo_seen && userland_seen {
                println!(
                    "qemu-loader: loader, high-half entry, smoke, IPC ring, VMO IPC, and USERLAND check all observed"
                );
                let _ = child.kill();
                let _ = child.wait();
                print_serial(&log);
                return Ok(());
            }
        }
        if let Some(status) = child
            .try_wait()
            .map_err(|err| format!("wait for qemu: {err}"))?
        {
            print_serial(&log);
            return Err(format!("QEMU exited before the boot completed ({status})"));
        }
        thread::sleep(Duration::from_millis(100));
    }
}

fn build_loader(root: &Path, out: &Path, loader_base: u64, test: bool) -> Result<PathBuf, String> {
    let clang = env::var("CLANG").unwrap_or_else(|_| String::from("clang"));
    let lld = env::var("LD_LLD").unwrap_or_else(|_| String::from("ld.lld"));
    let include = root.join("boot/cirrus-boot/include");
    let source_dir = root.join("boot/cirrus-boot/src");
    let objects_dir = out.join(if test {
        "loader-objects-test"
    } else {
        "loader-objects"
    });
    fs::create_dir_all(&objects_dir)
        .map_err(|err| format!("create {}: {err}", objects_dir.display()))?;

    let sources = [
        "entry.S",
        "loader.c",
        "boot_image.c",
        "boot_plan.c",
        "bootinfo.c",
        "initrd.c",
        "elf.c",
        "console.c",
        "support.c",
    ];
    let mut objects = Vec::new();
    for source in sources {
        let object = objects_dir.join(format!("{source}.o"));
        let mut command = Command::new(&clang);
        command
            .arg("--target=aarch64-none-elf")
            .arg("-ffreestanding")
            .arg("-fno-stack-protector")
            .arg("-fno-pic")
            .arg("-fno-builtin")
            .arg("-mgeneral-regs-only")
            .arg("-O2")
            .arg("-Wall")
            .arg("-Wextra")
            .arg("-Werror");
        if test {
            command.arg("-DCIRRUS_LOADER_TEST");
        }
        command
            .arg("-I")
            .arg(&include)
            .arg("-c")
            .arg(source_dir.join(source))
            .arg("-o")
            .arg(&object);
        run_command(&mut command, "clang (loader)")?;
        objects.push(object);
    }

    let elf = out.join(if test {
        "loader-test.elf"
    } else {
        "loader.elf"
    });
    let mut command = Command::new(&lld);
    command
        .arg("-T")
        .arg(root.join("boot/cirrus-boot/loader.ld"))
        .arg("--defsym")
        .arg(format!("LOADER_BASE=0x{loader_base:x}"))
        .arg("-static")
        .arg("-nostdlib")
        .arg("-o")
        .arg(&elf);
    for object in &objects {
        command.arg(object);
    }
    run_command(&mut command, "ld.lld (loader)")?;
    Ok(elf)
}

fn dump_qemu_dtb(qemu: &str, path: &Path) -> Result<(), String> {
    let _ = fs::remove_file(path);
    let status = Command::new(qemu)
        .arg("-M")
        .arg(format!("virt,gic-version=3,dumpdtb={}", path.display()))
        .arg("-cpu")
        .arg("cortex-a72")
        .arg("-m")
        .arg("1024")
        .arg("-display")
        .arg("none")
        .arg("-monitor")
        .arg("none")
        .arg("-serial")
        .arg("none")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map_err(|err| format!("spawn {qemu} (dumpdtb): {err}"))?;
    if !status.success() || !path.is_file() {
        return Err(format!(
            "could not generate QEMU virt DTB at {}",
            path.display()
        ));
    }
    Ok(())
}

fn patch_initrd_chosen(dtb: &Path, start: u64, len: u64) -> Result<(), String> {
    let fdtput = env::var("FDTPUT").unwrap_or_else(|_| String::from("fdtput"));
    let create = Command::new(&fdtput)
        .arg("-c")
        .arg("-p")
        .arg(dtb)
        .arg("/chosen")
        .status()
        .map_err(|err| format!("spawn {fdtput}: {err}"))?;
    if !create.success() {
        return Err("fdtput could not create /chosen".to_string());
    }
    for (property, value) in [
        ("linux,initrd-start", start),
        ("linux,initrd-end", start + len),
    ] {
        let status = Command::new(&fdtput)
            .arg("-t")
            .arg("x")
            .arg("-p")
            .arg(dtb)
            .arg("/chosen")
            .arg(property)
            .arg(format!("0x{value:x}"))
            .status()
            .map_err(|err| format!("spawn {fdtput}: {err}"))?;
        if !status.success() {
            return Err(format!("fdtput could not set {property}"));
        }
    }
    Ok(())
}

fn bootimg(args: &[String]) -> Result<(), String> {
    let mut slot = String::from("a");
    let mut profile = String::from("oneplus-enchilada");
    let mut dtb_path: Option<PathBuf> = None;
    let mut header_version: u32 = 1;
    let mut page_size: u32 = 4096;
    let mut cmdline = String::from("console=ttyMSM0,115200n8");
    let mut out_path: Option<PathBuf> = None;
    let mut gzip = true;
    let mut append_dtb = true;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--slot" => {
                i += 1;
                slot = args.get(i).cloned().ok_or("--slot needs a|b")?;
            }
            "--profile" => {
                i += 1;
                profile = args.get(i).cloned().ok_or("--profile needs a name")?;
            }
            "--dtb" => {
                i += 1;
                dtb_path = Some(PathBuf::from(
                    args.get(i).cloned().ok_or("--dtb needs a path")?,
                ));
            }
            "--header-version" => {
                i += 1;
                header_version = args
                    .get(i)
                    .and_then(|value| value.parse::<u32>().ok())
                    .ok_or("--header-version needs 0|1|2")?;
            }
            "--page-size" => {
                i += 1;
                page_size = args
                    .get(i)
                    .and_then(|value| value.parse::<u32>().ok())
                    .ok_or("--page-size needs a number")?;
            }
            "--cmdline" => {
                i += 1;
                cmdline = args.get(i).cloned().ok_or("--cmdline needs a value")?;
            }
            "--out" => {
                i += 1;
                out_path = Some(PathBuf::from(
                    args.get(i).cloned().ok_or("--out needs a path")?,
                ));
            }
            "--no-gzip" => gzip = false,
            "--no-append-dtb" => append_dtb = false,
            other => return Err(format!("unknown bootimg option '{other}'")),
        }
        i += 1;
    }

    if slot != "a" && slot != "b" {
        return Err(format!("unsupported slot '{slot}'; use a or b"));
    }
    if !profile.starts_with("oneplus-") {
        return Err(format!(
            "unsupported profile '{profile}'; use oneplus-enchilada or oneplus-fajita"
        ));
    }
    if header_version > 2 {
        return Err("--header-version must be 0, 1 or 2".to_string());
    }
    if dtb_path.is_some() && header_version < 2 && !append_dtb {
        return Err("a header < 2 carries no dtb section; append it or use header v2".to_string());
    }

    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .ok_or("xtask has no parent directory")?
        .to_path_buf();
    let build = root.join("build/bootimg");
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
    build_c_smoke(&root, &build)?;

    let kernel = root.join("target/aarch64-unknown-none/release/kumo-kernel");
    let smoke = build.join("c_smoke.elf");
    let kernel_bytes =
        fs::read(&kernel).map_err(|err| format!("read {}: {err}", kernel.display()))?;
    let smoke_bytes = fs::read(&smoke).map_err(|err| format!("read {}: {err}", smoke.display()))?;
    let ramdisk = build_initrd(&[
        ("boot/kumo-kernel.elf", kernel_bytes.as_slice()),
        ("bin/sora", smoke_bytes.as_slice()),
    ])?;
    let ramdisk_path = build.join("ramdisk.img");
    fs::write(&ramdisk_path, &ramdisk)
        .map_err(|err| format!("write {}: {err}", ramdisk_path.display()))?;

    let loader_elf = build_loader(&root, &build, ONEPLUS_LOADER_BASE, false)?;
    let objcopy = env::var("LLVM_OBJCOPY").unwrap_or_else(|_| String::from("llvm-objcopy"));
    let loader_bin = build.join("loader.bin");
    let mut command = Command::new(&objcopy);
    command
        .arg("-O")
        .arg("binary")
        .arg(&loader_elf)
        .arg(&loader_bin);
    run_command(&mut command, "llvm-objcopy")?;

    let mut kernel_blob =
        fs::read(&loader_bin).map_err(|err| format!("read {}: {err}", loader_bin.display()))?;
    if gzip {
        let gzip_tool = env::var("GZIP").unwrap_or_else(|_| String::from("gzip"));
        let output = Command::new(&gzip_tool)
            .arg("-n")
            .arg("-9")
            .arg("-c")
            .arg(&loader_bin)
            .output()
            .map_err(|err| format!("spawn {gzip_tool}: {err}"))?;
        if !output.status.success() {
            return Err("gzip failed on the loader binary".to_string());
        }
        kernel_blob = output.stdout;
    }
    let dtb_bytes = match &dtb_path {
        Some(path) => {
            Some(fs::read(path).map_err(|err| format!("read {}: {err}", path.display()))?)
        }
        None => None,
    };
    if header_version < 2 {
        if let Some(bytes) = &dtb_bytes {
            kernel_blob.extend_from_slice(bytes);
        }
    }
    let kernel_blob_path = build.join("kernel.bin");
    fs::write(&kernel_blob_path, &kernel_blob)
        .map_err(|err| format!("write {}: {err}", kernel_blob_path.display()))?;

    let clang = env::var("CLANG").unwrap_or_else(|_| String::from("clang"));
    let tool = build.join("bootimg");
    let mut command = Command::new(&clang);
    command
        .arg("-std=c11")
        .arg("-Wall")
        .arg("-Wextra")
        .arg("-Werror")
        .arg(format!(
            "-I{}",
            root.join("boot/cirrus-boot/include").display()
        ))
        .arg(root.join("boot/cirrus-boot/src/boot_image.c"))
        .arg(root.join("boot/cirrus-boot/tools/bootimg.c"))
        .arg("-o")
        .arg(&tool);
    run_command(&mut command, "clang (bootimg)")?;

    let image = out_path.unwrap_or_else(|| build.join(format!("boot_{slot}.img")));
    let mut command = Command::new(&tool);
    command
        .arg("pack")
        .arg(&image)
        .arg("--page-size")
        .arg(page_size.to_string())
        .arg("--header-version")
        .arg(header_version.to_string())
        .arg("--base")
        .arg("0x80000000")
        .arg("--kernel-offset")
        .arg("0x00008000")
        .arg("--ramdisk-offset")
        .arg("0x02000000")
        .arg("--second-offset")
        .arg("0x00f00000")
        .arg("--tags-offset")
        .arg("0x01e00000")
        .arg("--cmdline")
        .arg(&cmdline)
        .arg(&kernel_blob_path)
        .arg(&ramdisk_path);
    if header_version >= 2 {
        if let Some(path) = &dtb_path {
            command.arg(path);
        }
    }
    run_command(&mut command, "bootimg pack")?;

    println!("profile: {profile} slot {slot}");
    println!("image:   {}", image.display());
    println!(
        "kernel:  {} bytes ({}gzip)",
        kernel_blob.len(),
        if gzip { "" } else { "no " }
    );
    println!("ramdisk: {} bytes", ramdisk.len());
    if let Some(path) = &dtb_path {
        println!("dtb:     {}", path.display());
    }
    return Ok(());
}

fn stage_esp(args: &[String]) -> Result<(), String> {
    let mut board_id = String::from("qemu-virt-aarch64");
    let mut out: Option<PathBuf> = None;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--board" => {
                i += 1;
                board_id = args.get(i).cloned().ok_or("--board needs an id")?;
            }
            "--out" => {
                i += 1;
                out = Some(PathBuf::from(
                    args.get(i).cloned().ok_or("--out needs a path")?,
                ));
            }
            other => return Err(format!("unknown stage-esp option '{other}'")),
        }
        i += 1;
    }

    let board = Board::from_id(&board_id)
        .ok_or_else(|| format!("unknown board '{board_id}'; see kumo-bsp Board::from_id"))?;

    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .ok_or("xtask has no parent directory")?
        .to_path_buf();
    let build = root.join("build/esp");
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
    let smoke = build.join("c_smoke.elf");
    if !kernel.is_file() {
        return Err(format!("kernel ELF missing: {}", kernel.display()));
    }
    if !loader.is_file() {
        return Err(format!("niji-uefi missing: {}", loader.display()));
    }
    let kernel_bytes =
        fs::read(&kernel).map_err(|err| format!("read {}: {err}", kernel.display()))?;
    let smoke_bytes = fs::read(&smoke).map_err(|err| format!("read {}: {err}", smoke.display()))?;
    let initrd = build_initrd(&[
        ("boot/kumo-kernel.elf", kernel_bytes.as_slice()),
        ("bin/sora", smoke_bytes.as_slice()),
    ])?;
    let initrd_path = build.join("initrd.img");
    fs::write(&initrd_path, &initrd)
        .map_err(|err| format!("write {}: {err}", initrd_path.display()))?;

    let esp = out.unwrap_or_else(|| build.join(board.id()));
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
    let manifest = format!(
        "kernel = \\EFI\\KUMO\\kernel\\kumo-kernel.elf\n\
         initrd = \\EFI\\KUMO\\initrd.img\n\
         dtb = firmware\n\
         board = {}\n",
        board.id()
    );
    fs::write(esp.join("EFI/KUMO/nijigumo.conf"), manifest)
        .map_err(|err| format!("stage boot manifest: {err}"))?;

    println!("board:   {}", board.id());
    match board.spec().console {
        Console::Pl011 { base } => println!("console: PL011 {base:#x}"),
        Console::Dw8250 { base } => println!("console: DW-APB {base:#x}"),
        Console::Geni { base } => println!("console: GENI {base:#x}"),
        Console::Framebuffer => println!("console: framebuffer only"),
    }
    println!("ESP:     {}", esp.display());
    println!("kernel:  {}", kernel.display());
    println!(
        "initrd:  {} ({} bytes)",
        initrd_path.display(),
        initrd.len()
    );
    if matches!(board, Board::OnePlus6Enchilada | Board::OnePlus6T) {
        println!("note:    OnePlus targets boot through `cargo xtask bootimg`, not a UEFI ESP");
    }
    Ok(())
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
        ("userland/ipc_ring.c", "ipc_ring.o"),
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
        .arg(out.join("c_smoke.o"))
        .arg(out.join("ipc_ring.o"));
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
    let mut ring_seen = false;
    let mut vmo_seen = false;
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
                println!("qemu-smoke: C marker observed");
            }
            if !ring_seen && text.contains(RING_MARKER) {
                ring_seen = true;
                println!("qemu-smoke: C IPC ring marker observed");
            }
            if !vmo_seen && text.contains(VMO_IPC_MARKER) {
                vmo_seen = true;
                println!("qemu-smoke: C VMO IPC marker observed");
            }
            if text.contains("CIRRUS C IPC ring: FAIL") {
                let _ = child.kill();
                let _ = child.wait();
                print_serial(log);
                return Err("C IPC ring reported failure".to_string());
            }
            if text.contains("CIRRUS C VMO IPC: FAIL") {
                let _ = child.kill();
                let _ = child.wait();
                print_serial(log);
                return Err("C VMO IPC reported failure".to_string());
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

            if smoke_seen && ring_seen && vmo_seen && userland_seen {
                println!(
                    "qemu-smoke: smoke, IPC ring, VMO IPC, and post-exit USERLAND check all observed"
                );
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
                "CIRRUS C VMO IPC: FAIL",
            ] {
                if text.contains(failure) {
                    let _ = child.kill();
                    let _ = child.wait();
                    print_serial(log);
                    return Err(format!(
                        "qemu-smoke failed: transcript contains '{failure}'"
                    ));
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
