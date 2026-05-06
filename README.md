# Logscan

**Logscan** is a small command‑line utility in this repository. This README describes exactly what is in the repo, how the provided `install.sh` behaves, and how to **build**, **install**, **use**, **verify**, and **uninstall** the utility. No features are invented — everything below reflects the repository layout and the installer script behavior.

---

## Repository layout

- **`src/logscan.c`** — primary C source required by the build script.  
- **`src/logscan_noncurses.c`** — fallback non‑ncurses source; the installer writes this file if ncurses is not available.  
- **`man/logscan.1`** — optional man page (installed only if present).  
- **`install.sh`** — build and install script with modes: `install`, `build-only`, `uninstall`, `help`.  
- **`Makefile`** — optional helper (if present).

---

## Installer behavior (exact)

- **Modes supported**
  - `install` — ensure build tools, attempt to build, then install binary and man page.
  - `build-only` — compile the binary in the current directory only (no system install).
  - `uninstall` — remove installed binary and man page.
  - `help` — print usage and exit.

- **Build logic**
  1. The script **requires** `src/logscan.c`. If that file is missing the script exits with an error.
  2. It checks for `gcc`. If `gcc` is missing the script attempts to install build tools using the detected package manager (`apt`, `dnf`, `yum`, `pacman`, `apk`, `brew`) where available. If no package manager is detected it prints instructions to install build tools manually.
  3. It tests whether ncurses development headers and library are usable by compiling a tiny test program linked with `-lncurses`.
     - If the test succeeds, the script compiles `src/logscan.c` with `-lncurses`.
     - If the test fails, the script attempts to install ncurses dev packages via the detected package manager.
     - If ncurses still cannot be used, the script writes a small fallback source file `src/logscan_noncurses.c` (a minimal ANSI fallback) and compiles that instead.
  4. On success the script produces a local binary named **`logscan`** in the current directory.

- **Install logic**
  - `install` mode installs the binary to **`/usr/local/bin/logscan`** (mode `0755`).
  - If `man/logscan.1` exists, it installs it to **`/usr/local/share/man/man1/logscan.1`** (mode `0644`).
  - If `mandb` is available the script runs `mandb` (errors ignored).
  - `sudo` is required for the install steps that write to system locations.

- **Uninstall logic**
  - Removes `/usr/local/bin/logscan` and `/usr/local/share/man/man1/logscan.1` (if present) and runs `mandb` if available.

---

## How to build and install (exact commands)

Run these commands from the repository root.

**Clone and prepare**
```bash
git clone https://github.com/nnotych/LinuxLogScanTool.git
cd LinuxLogScanTool
chmod +x install.sh
```

**Build only (no install)**
```bash
./install.sh build-only
# On success a local ./logscan binary will be created
```

**Install system‑wide**
```bash
sudo ./install.sh install
# Installs /usr/local/bin/logscan and, if present, /usr/local/share/man/man1/logscan.1
```

**Uninstall**
```bash
sudo ./install.sh uninstall
```

**Alternative (Makefile)**
```bash
make
# If Makefile is present and configured, it may produce the binary without using install.sh
```

---

## Where files are placed after install

- **Binary**: `/usr/local/bin/logscan`  
- **Man page** (if present): `/usr/local/share/man/man1/logscan.1`

---

## How to run and verify

- **Run the built binary in the current directory**
```bash
./logscan --help
```

- **Run the installed binary**
```bash
logscan --help
# or
/usr/local/bin/logscan --help
```

- **Check man page**
```bash
man logscan
```

- **Verify installed files**
```bash
ls -l /usr/local/bin/logscan
ls -l /usr/local/share/man/man1/logscan.1
```
![Uploading зображення.png…]()


> Note: The installer script does not itself document runtime CLI flags. Use `logscan --help` or `man logscan` (if installed) to see the exact runtime options provided by the compiled binary.

---

## Troubleshooting (concrete steps)

- **`Source file src/logscan.c not found`**  
  - Ensure `src/logscan.c` exists in the repository root. The installer requires this file.

- **`gcc not found`**  
  - Install build tools manually. Example for Debian/Ubuntu:
    ```bash
    sudo apt-get update
    sudo apt-get install -y build-essential
    ```

- **ncurses test fails and fallback is used**  
  - To get the ncurses build (interactive mode), install the dev package for your distro:
    - Debian/Ubuntu:
      ```bash
      sudo apt-get install -y libncurses5-dev libncursesw5-dev
      ```
    - Fedora/CentOS:
      ```bash
      sudo dnf install -y ncurses-devel
      ```
  - If you cannot install ncurses, the script will compile `src/logscan_noncurses.c` automatically and produce a fallback binary.

- **Permission denied during install**  
  - Use `sudo ./install.sh install`.

- **Man page not installed**  
  - The script installs the man page only if `man/logscan.1` exists in the repository. Add or update that file if you need a man page installed.

- **`mandb` not available or errors updating man database**  
  - The script ignores `mandb` errors; man pages still install to the target directory.

---

## Minimal checklist before running `install.sh`

- `src/logscan.c` is present.  
- You have `sudo` access for `install` mode.  
- `gcc` and basic build tools are installed or available to be installed.  
- If you need interactive ncurses support, ensure ncurses dev packages are available on the system.

---

## License and contribution

- Add a `LICENSE` file to the repository root if you want to declare a license (for example, MIT).  
- Contribution workflow: fork → branch → commit → pull request. Document any new runtime flags in `--help` and `man/logscan.1`.

---

If you want this exact content written to a `README.md` file in the repository, I can produce the file contents for you to paste into `README.md`.
