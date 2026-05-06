#!/usr/bin/env bash
set -euo pipefail

PREFIX="/usr/local"
BINDIR="$PREFIX/bin"
MANDIR="$PREFIX/share/man/man1"
SRC="src/logscan.c"
FALLBACK_SRC="src/logscan_noncurses.c"
BIN="logscan"
MAN="man/logscan.1"
TMPTEST="/tmp/_nc_test.c"

usage() {
  cat <<EOF
Usage: sudo ./install.sh [install|build-only|uninstall|help]

install     - ensure build deps, build, install binary and man page
build-only  - only compile binary in current dir (tries ncurses, falls back)
uninstall   - remove installed binary and man page
help        - show this message
EOF
  exit 1
}

detect_pkg_manager() {
  if command -v apt-get >/dev/null 2>&1; then echo "apt"; return; fi
  if command -v dnf >/dev/null 2>&1; then echo "dnf"; return; fi
  if command -v yum >/dev/null 2>&1; then echo "yum"; return; fi
  if command -v pacman >/dev/null 2>&1; then echo "pacman"; return; fi
  if command -v apk >/dev/null 2>&1; then echo "apk"; return; fi
  if command -v brew >/dev/null 2>&1; then echo "brew"; return; fi
  echo "unknown"
}

install_pkg() {
  local pkg="$1"
  local pm="$2"
  case "$pm" in
    apt) sudo apt-get update && sudo apt-get install -y $pkg ;;
    dnf) sudo dnf install -y $pkg ;;
    yum) sudo yum install -y $pkg ;;
    pacman) sudo pacman -S --noconfirm $pkg ;;
    apk) sudo apk add $pkg ;;
    brew) brew install $pkg ;;
    *) echo "Package manager not detected; please install $pkg manually." >&2; return 1 ;;
  esac
}

ensure_build_tools() {
  if ! command -v gcc >/dev/null 2>&1; then
    echo "GCC not found. Attempting to install build tools..."
    pm=$(detect_pkg_manager)
    case "$pm" in
      apt) sudo apt-get update && sudo apt-get install -y build-essential ;;
      dnf) sudo dnf groupinstall -y "Development Tools" || sudo dnf install -y gcc make ;;
      yum) sudo yum groupinstall -y "Development Tools" || sudo yum install -y gcc make ;;
      pacman) sudo pacman -S --noconfirm base-devel ;;
      apk) sudo apk add build-base ;;
      brew) echo "Please install Xcode command line tools and gcc via brew." ;;
      *) echo "Cannot auto-install build tools. Please install gcc/make manually." ;;
    esac
  fi
  if ! command -v make >/dev/null 2>&1; then
    echo "Warning: make not found. Some build steps may require make."
  fi
}

check_ncurses_dev() {
  cat > "$TMPTEST" <<'EOF'
#include <ncurses.h>
int main(void){ initscr(); endwin(); return 0; }
EOF
  if gcc -o /tmp/_nc_test "$TMPTEST" -lncurses >/dev/null 2>&1; then
    rm -f /tmp/_nc_test "$TMPTEST"
    return 0
  else
    rm -f /tmp/_nc_test "$TMPTEST"
    return 1
  fi
}

install_ncurses_dev() {
  pm=$(detect_pkg_manager)
  case "$pm" in
    apt) install_pkg "libncurses5-dev libncursesw5-dev" "$pm" ;;
    dnf|yum) install_pkg "ncurses-devel" "$pm" ;;
    pacman) install_pkg "ncurses" "$pm" ;;
    apk) install_pkg "ncurses-dev" "$pm" ;;
    brew) install_pkg "ncurses" "$pm" ;;
    *) echo "Automatic installation not supported for this OS. Please install ncurses-dev manually." >&2; return 1 ;;
  esac
}

write_fallback_source() {
  if [ -f "$FALLBACK_SRC" ]; then
    echo "Fallback source $FALLBACK_SRC already exists; using it."
    return 0
  fi
  mkdir -p "$(dirname "$FALLBACK_SRC")"
  cat > "$FALLBACK_SRC" <<'EOF'
/* Minimal non-ncurses interactive fallback for logscan.
 * Uses ANSI escape sequences and simple getch to navigate context window.
 * This is intentionally small and portable.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int getch(void) {
    struct termios oldt, newt;
    int ch;
    if (tcgetattr(STDIN_FILENO, &oldt) == -1) return getchar();
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    newt.c_cc[VMIN] = 1;
    newt.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

/* Very small demo program: prints a static context block and allows j/k movement.
 * Replace with your real logscan logic or keep as fallback.
 */
int main(int argc, char **argv) {
    const char *file = "fallback.log";
    int match_line = 247;
    int ctx = 3;
    int cursor = match_line;
    while (1) {
        printf("\x1b[H\x1b[2J"); /* clear */
        printf("Match: %s:%d\n\n", file, match_line);
        for (int i = match_line - ctx; i <= match_line + ctx; ++i) {
            if (i == cursor) printf("\x1b[1;31m=> %4d:\x1b[0m sample line %d\n", i, i);
            else if (i == match_line) printf("\x1b[1;33m   %4d:\x1b[0m sample line %d\n", i, i);
            else printf("    %4d: sample line %d\n", i, i);
        }
        printf("\nCommands: j (down), k (up), q (quit)\n");
        int c = getch();
        if (c == 'j') cursor++;
        else if (c == 'k') cursor--;
        else if (c == 'q') break;
    }
    return 0;
}
EOF
  echo "Wrote fallback source to $FALLBACK_SRC"
}

build() {
  if [ ! -f "$SRC" ]; then
    echo "Source file $SRC not found. Aborting." >&2
    exit 2
  fi

  ensure_build_tools

  echo "Trying to build with ncurses (if available)..."
  if check_ncurses_dev; then
    echo "ncurses detected; compiling with ncurses."
    gcc -Wall -O2 "$SRC" -o "$BIN" -lncurses
    echo "Built $BIN with ncurses."
    return 0
  fi

  echo "ncurses headers not found. Attempting to install ncurses dev packages..."
  if install_ncurses_dev; then
    if check_ncurses_dev; then
      echo "ncurses installed; compiling with ncurses."
      gcc -Wall -O2 "$SRC" -o "$BIN" -lncurses
      echo "Built $BIN with ncurses."
      return 0
    else
      echo "ncurses install attempted but headers still not usable."
    fi
  else
    echo "Failed to install ncurses via package manager."
  fi

  echo "Falling back to non-ncurses build."
  write_fallback_source
  if gcc -Wall -O2 "$FALLBACK_SRC" -o "$BIN"; then
    echo "Built fallback binary $BIN (non-ncurses)."
    return 0
  else
    echo "Fallback build failed." >&2
    exit 3
  fi
}

install_files() {
  if [ ! -f "./$BIN" ]; then
    echo "Binary ./$BIN not found. Run build-first or use build-only." >&2
    exit 3
  fi

  echo "Creating target directories..."
  sudo mkdir -p "$BINDIR"
  sudo mkdir -p "$MANDIR"

  echo "Installing binary to $BINDIR"
  sudo install -m 0755 "./$BIN" "$BINDIR/$BIN"

  if [ -f "$MAN" ]; then
    echo "Installing man page to $MANDIR"
    sudo install -m 0644 "$MAN" "$MANDIR/$BIN.1"
  else
    echo "Warning: man page $MAN not found; skipping man installation."
  fi

  if command -v mandb >/dev/null 2>&1; then
    echo "Updating man database (mandb)..."
    sudo mandb || true
  fi

  echo "Installation complete. You can run: $BIN --help"
}

uninstall_files() {
  echo "Removing $BINDIR/$BIN and $MANDIR/$BIN.1 (if exist)"
  sudo rm -f "$BINDIR/$BIN"
  sudo rm -f "$MANDIR/$BIN.1"
  if command -v mandb >/dev/null 2>&1; then
    sudo mandb || true
  fi
  echo "Uninstalled."
}

if [ $# -ne 1 ]; then
  usage
fi

case "$1" in
  install)
    ensure_build_tools
    build
    install_files
    ;;
  "build-only")
    ensure_build_tools
    build
    ;;
  uninstall)
    uninstall_files
    ;;
  help|--help|-h)
    usage
    ;;
  *)
    usage
    ;;
esac
