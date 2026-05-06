# Logscan

Logscan is a lightweight command-line utility for system administrators to search through log files and highlight lines containing specific keywords.  
It supports scanning entire directories or individual files, with options for case-insensitive search and summary statistics.

---

## ✨ Features
- Search for keywords in all files within a log directory
- Highlight matching lines with ANSI colors
- Case-insensitive search (`-i`)
- Scan only a specific file (`-f <filename>`)
- Summary of matches per file and overall
- Simple usage help (`--help`)
- Man page included (`man logscan`)

---

## 📦 Installation
Clone the repository and build with `make`:

```bash
git clone https://github.com/USERNAME/logscan.git
cd logscan
make
sudo make install
# -langLinuxutilsSHIT
