/* src/logscan.c
 *
 * logscan - simple log searching tool with ncurses interactive viewer
 *
 * Features:
 *  - Non-interactive: prints matched lines exactly as in files (cat-style).
 *  - Interactive (-I): ncurses list of matches; supports j/k, Up/Down, n/p, Enter/o to open editor, q to quit.
 *  - Safe editor invocation via /bin/sh -c to support complex $EDITOR values.
 *  - Proper terminal state save/restore around external editor (def_prog_mode/reset_prog_mode).
 *
 * Build:
 *   gcc -Wall -O2 src/logscan.c -o logscan -lncurses
 *
 * Options:
 *   -i           case-insensitive (default)
 *   -c           case-sensitive
 *   -w           whole-word match
 *   -f <file>    scan only specific file name (basename)
 *   -I, --interactive  interactive navigation
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <ncurses.h>

typedef struct {
    char *file;   /* full path */
    int line;     /* 1-based */
    char *text;   /* line text including newline */
} Match;

static Match *matches = NULL;
static size_t matches_count = 0;
static size_t matches_cap = 0;

static void add_match(const char *file, int line, const char *text) {
    if (matches_count == matches_cap) {
        matches_cap = matches_cap ? matches_cap * 2 : 256;
        matches = realloc(matches, matches_cap * sizeof(Match));
        if (!matches) { perror("realloc"); exit(1); }
    }
    matches[matches_count].file = strdup(file);
    matches[matches_count].line = line;
    matches[matches_count].text = strdup(text);
    matches_count++;
}

static void free_matches(void) {
    for (size_t i = 0; i < matches_count; ++i) {
        free(matches[i].file);
        free(matches[i].text);
    }
    free(matches);
    matches = NULL;
    matches_count = matches_cap = 0;
}

/* word character test */
static int is_word_char(char c) {
    return (isalnum((unsigned char)c) || c == '_');
}

/* search: supports case sensitivity and whole-word */
static int contains_exact(const char *line, const char *keyword, int case_sensitive, int whole_word) {
    size_t K = strlen(keyword);
    if (K == 0) return 0;
    size_t L = strlen(line);
    for (size_t i = 0; i + K <= L; ++i) {
        size_t j = 0;
        for (; j < K; ++j) {
            char a = line[i+j];
            char b = keyword[j];
            if (!case_sensitive) { a = tolower((unsigned char)a); b = tolower((unsigned char)b); }
            if (a != b) break;
        }
        if (j == K) {
            if (whole_word) {
                if (i > 0 && is_word_char(line[i-1])) continue;
                if (i + (int)K < (int)L && is_word_char(line[i+K])) continue;
            }
            return 1;
        }
    }
    return 0;
}

/* scan a single file by full path */
static void scan_file_path(const char *path, const char *keyword, int case_sensitive, int whole_word) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *line = NULL;
    size_t len = 0;
    int lineno = 0;
    while (getline(&line, &len, f) != -1) {
        lineno++;
        if (contains_exact(line, keyword, case_sensitive, whole_word)) {
            add_match(path, lineno, line);
        }
    }
    free(line);
    fclose(f);
}

/* scan directory non-recursive; only_file is basename or NULL */
static void scan_dir(const char *dirpath, const char *keyword, int case_sensitive, int whole_word, const char *only_file) {
    DIR *d = opendir(dirpath);
    if (!d) {
        fprintf(stderr, "Cannot open directory %s: %s\n", dirpath, strerror(errno));
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (only_file && strcmp(ent->d_name, only_file) != 0) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dirpath, ent->d_name);
        struct stat st;
        if (stat(path, &st) == -1) continue;
        if (!S_ISREG(st.st_mode)) continue;
        scan_file_path(path, keyword, case_sensitive, whole_word);
    }
    closedir(d);
}

/* open editor using /bin/sh -c "editor +<line> file"
   This allows complex $EDITOR values (with args) and ensures child runs in shell.
*/
static int open_editor_with(const char *editor, const char *file, int line) {
    if (!editor || strlen(editor) == 0) return -1;

    /* build safe command: quote file path */
    char cmd[4096];
    /* Use simple quoting for file: wrap in single quotes and escape existing single quotes */
    char file_quoted[4096];
    const char *p = file;
    char *q = file_quoted;
    *q++ = '\'';
    while (*p && (size_t)(q - file_quoted) < sizeof(file_quoted) - 4) {
        if (*p == '\'') {
            /* close, escape, reopen: '\'' */
            strcpy(q, "'\\''");
            q += 4;
        } else {
            *q++ = *p;
        }
        p++;
    }
    *q++ = '\'';
    *q = '\0';

    /* Many editors accept +<line> file syntax; place +line before filename */
    snprintf(cmd, sizeof(cmd), "%s +%d %s", editor, line, file_quoted);

    pid_t pid = fork();
    if (pid == 0) {
        /* child: run via /bin/sh -c "cmd" so complex editor strings work */
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return -1;
}

/* print matches exactly like cat (non-interactive) */
static void print_all_matches_cat_style(void) {
    for (size_t i = 0; i < matches_count; ++i) {
        if (matches[i].text) fputs(matches[i].text, stdout);
    }
}

/* interactive ncurses list view */
static void interactive_ncurses(const char *keyword, int case_sensitive, int whole_word) {
    if (matches_count == 0) {
        printf("No matches found.\n");
        return;
    }

    /* initialize ncurses */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_BLACK, COLOR_GREEN); /* selection */
    }

    size_t idx = 0;
    size_t offset = 0;

    while (1) {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int usable = rows - 4;
        if (usable < 3) usable = 3;

        if ((int)idx < (int)offset) offset = idx;
        if (idx >= offset + (size_t)usable) offset = idx - usable + 1;

        clear();
        mvprintw(0, 0, "Matches %zu/%zu  (showing %zu-%zu)", idx+1, matches_count, offset+1, (offset + usable < matches_count) ? offset+usable : matches_count);
        mvprintw(1, 0, "Commands: j/k or Down/Up move, n/p next/prev, Enter/o open editor, q quit");

        for (size_t i = offset; i < matches_count && (int)(i - offset) < usable; ++i) {
            int y = 3 + (int)(i - offset);
            /* prepare display text without trailing newline */
            char *txt = strdup(matches[i].text ? matches[i].text : "");
            if (txt) {
                size_t L = strlen(txt);
                if (L > 0 && txt[L-1] == '\n') txt[L-1] = '\0';
            }
            if (i == idx) {
                if (has_colors()) attron(COLOR_PAIR(1));
                mvprintw(y, 0, "%4zu: %s:%d: %s", i+1, matches[i].file, matches[i].line, txt ? txt : "");
                if (has_colors()) attroff(COLOR_PAIR(1));
            } else {
                mvprintw(y, 0, "%4zu: %s:%d: %s", i+1, matches[i].file, matches[i].line, txt ? txt : "");
            }
            free(txt);
        }
        refresh();

        int ch = getch();
        if (ch == 'j' || ch == KEY_DOWN) {
            if (idx + 1 < matches_count) idx++;
            else beep();
        } else if (ch == 'k' || ch == KEY_UP) {
            if (idx > 0) idx--;
            else beep();
        } else if (ch == 'n') {
            if (idx + 1 < matches_count) idx++;
            else beep();
        } else if (ch == 'p') {
            if (idx > 0) idx--;
            else beep();
        } else if (ch == '\n' || ch == '\r' || ch == 'o') {
            /* prompt for editor choice at bottom */
            echo();
            nocbreak();
            nodelay(stdscr, FALSE);
            int r = getmaxy(stdscr);
            mvprintw(r-1, 0, "Choose editor: 1) nano 2) vim 3) $EDITOR (empty = $EDITOR) : ");
            clrtoeol();
            char choice[128] = {0};
            mvgetnstr(r-1, 0, choice, (int)sizeof(choice)-1);
            noecho();
            cbreak();
            nodelay(stdscr, FALSE);

            char editor_cmd[256] = {0};
            if (strlen(choice) == 0) {
                const char *env = getenv("EDITOR");
                if (env && strlen(env) > 0) strncpy(editor_cmd, env, sizeof(editor_cmd)-1);
                else strncpy(editor_cmd, "vi", sizeof(editor_cmd)-1);
            } else if (strcmp(choice, "1") == 0) strncpy(editor_cmd, "nano", sizeof(editor_cmd)-1);
            else if (strcmp(choice, "2") == 0) strncpy(editor_cmd, "vim", sizeof(editor_cmd)-1);
            else if (strcmp(choice, "3") == 0) {
                const char *env = getenv("EDITOR");
                if (env && strlen(env) > 0) strncpy(editor_cmd, env, sizeof(editor_cmd)-1);
                else strncpy(editor_cmd, "vi", sizeof(editor_cmd)-1);
            } else {
                strncpy(editor_cmd, choice, sizeof(editor_cmd)-1);
            }

            const char *file = matches[idx].file;
            int line = matches[idx].line;

            /* save curses state, endwin, run editor, restore curses state */
            def_prog_mode();   /* save tty modes for ncurses */
            endwin();
            open_editor_with(editor_cmd, file, line);
            reset_prog_mode(); /* restore tty modes saved by def_prog_mode */
            refresh();

            /* after editor: filter matches to this file only */
            free_matches();
            scan_file_path(file, keyword, case_sensitive, whole_word);

            /* if no matches left, exit interactive */
            if (matches_count == 0) {
                endwin();
                printf("No matches for \"%s\" in %s\n", keyword, file);
                return;
            }
            idx = 0;
            offset = 0;
        } else if (ch == 'q') {
            break;
        } else {
            /* ignore other keys */
        }
    }

    endwin();
}

/* usage */
static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <logdir> <keyword> [options]\n"
        "Options:\n"
        "  -i           case-insensitive (default)\n"
        "  -c           case-sensitive\n"
        "  -w           whole-word match\n"
        "  -f <file>    scan only specific file name (basename)\n"
        "  -I, --interactive  interactive navigation\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    const char *dirpath = argv[1];
    const char *keyword = argv[2];
    int case_sensitive = 0; /* default insensitive */
    int whole_word = 0;
    const char *only_file = NULL;
    int interactive = 0;

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0) case_sensitive = 0;
        else if (strcmp(argv[i], "-c") == 0) case_sensitive = 1;
        else if (strcmp(argv[i], "-w") == 0) whole_word = 1;
        else if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--interactive") == 0) interactive = 1;
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) { only_file = argv[++i]; }
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(argv[0]); return 1; }
    }

    if (!keyword || strlen(keyword) == 0) {
        fprintf(stderr, "Error: empty keyword\n");
        return 1;
    }

    scan_dir(dirpath, keyword, case_sensitive, whole_word, only_file);

    if (interactive) interactive_ncurses(keyword, case_sensitive, whole_word);
    else print_all_matches_cat_style();

    free_matches();
    return 0;
}
