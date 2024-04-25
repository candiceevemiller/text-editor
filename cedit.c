/* Name: Cedit
 * Purpose: Simple text editor Cedit - Candice's Editor
 * Author: Candice Miller
 */

//TODO: REFACTOR INTO SEVERAL FILES

/*** includes***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

/*** defines ***/
#define CEDIT_VERSION "0.0.1"
#define TAB_STOP_SIZE 4

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

/*** data ***/

typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct editorConfig {
    int cx, cy;
    int rx;
    int rowoff; // row offset for scroll
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/
void die(const char* s);
void enableRawMode();
void disableRawMode();
int editorReadKey();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

/*** row operations ***/
void editorAppendRow(char *s, size_t len);
void editorUpdateRow(erow *row);
int editorRowCxToRx(erow *row, int cx);

/*** file i/o ***/

void editorOpen(char* filename);

/*** append buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

/*** input ***/
void editorMoveCursor(int key); 
void editorProcessKeypress();

/*** output ***/
void moveCursorToStart();
void clearScreen();
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorScroll();
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);

/*** init ***/
void initEditor();

int main(int argc, char *argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-Q = quit");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}

/*** terminal ***/
void enableRawMode()
{
    // Get original terminal config
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    // Automatically run disableRawMode at program exit
    atexit(disableRawMode);
    
    // Copy orig config into raw
    struct termios raw = E.orig_termios;

    /* Set the terminal config flags to what we want using bitwise arithmetic
     * & (and) and ~ (not) force these flags to be false
     * All code comes from termios.h
     *
     * 
     * INPUT FLAGS - c_iflags:
     * -------------------------------
     * IXON - handles ctrl-s ctrl-q (suspend/resume)
     * ICRNL - CR carriage return NL new line. Replaces \r with \n on ctrl-m
     * Likely useless on modern terminals but part of turning on raw mode:
     * BRKINT - sends interrupt (ctrl-c SIGINT) on a break condition
     * INPCK - checks parity
     * ISTRIP - strips 8th bit of each input byte
     * 
     * OUTPUT FLAGS - c_oflags:
     * -------------------------------
     * OPOST - output post processing converts \n to \r\n related to ICRNL
     *         practically this means we're using \r\n manually in this file
     * 
     * CONTROL FLAGS - c_cflags:
     * -------------------------------
     * CS8 - Not a flag, a mask with multiple bits - sets char size to 8 bits
     *
     * LOCAL FLAGS - c_lflags:
     * -------------------------------
     * ECHO - echos what you write to the terminal
     * ICANON - reads by line instead of by byte
     * ISIG - controls ctrl-c ctrl-z (interrupt / and suspend to background)
     * IEXTEN - handles ctrl-v
     * 
     * We want byte-wise reading, no echo, override escape chars
     */
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    raw.c_cc[VMIN] = 0; // Sets minimum bytes to 0 - reads input after there's any input to be read
    raw.c_cc[VTIME] = 1; // Times out input read after 1/10th second

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

void disableRawMode()
{
    // Try to set terminal back to orignial config
    // && Flush any trailing characters (TCSAFLUSH)
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void die(const char* s)
{
    // Basic Error Logging and exit

    //cls first so error message not overwritten
    clearScreen();

    perror(s); //print error
    exit(1); //return 1 (error) on program exit
}

int editorReadKey()
{
    // Contains logic to read key presses
    int nread;
    char c = '\0';
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        // Home and End have multiple possible esc sequences
                        case '1':
                        case '7':
                            return HOME_KEY;
                        case '4':
                        case '8':
                            return END_KEY;    
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
            }
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';    
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    // prints cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    // reads cursor position into buffer
    while (i < sizeof(buf) -1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }

    // terminates buffer string
    buf[i] = '\0';

    // confirms buffer is escape sequence
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    // reads buffer into pointers for rows and cols
    // no & because our function params are pointers
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    // Terminal Input/Output Control Get Window Size
    // TIOCGWINSZ
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // moves cursor down and right 999 times
        // C and B prevent leaving edge of screen
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows,cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
}

void editorUpdateRow(erow *row)
{
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;
    
    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_STOP_SIZE - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP_SIZE != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

int editorRowCxToRx(erow *row, int cx)
{
    // Handles rendering for chars that take up extra width
    // like tab stops
    int rx = 0;
    int j;
    for (j=0; j < cx; j++) {
        if (row->chars[j] == '\t')
        rx += (TAB_STOP_SIZE - 1) - (rx % TAB_STOP_SIZE);
        rx++;
    }
    return rx;
}

/*** file i/o ***/

void editorOpen(char *filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                            line[linelen - 1] == '\r'))
        linelen --;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

/*** append buffer ***/
void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len+len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

/*** input ***/
void editorMoveCursor(int key)
{
    // Handle cursor movement

    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c) {
        // exit program
        case CTRL_KEY('q'):
            clearScreen();
            exit(0);
            break;

        // Page Up/Page Down
        case PAGE_UP:
        case PAGE_DOWN:
        {
            if (c == PAGE_UP) {
                E.cy = E.rowoff;
            } else if (c == PAGE_DOWN) {
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows) E.cy = E.numrows;
            }
            
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;

        // Home and End
        case HOME_KEY:
            E.cx=0;
            break;

        case END_KEY:
            if (E.cy < E.numrows)
            E.cx = E.row[E.cy].size;
            break;

        // Move Cursor
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

void moveCursorToStart()
{
    // [H moves cursor to top of terminal
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** output ***/
void clearScreen()
{
    // [2J clears whole screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    moveCursorToStart();
}

void editorDrawRows(struct abuf *ab)
{
    int r; // rows
    for (r = 0; r < E.screenrows; r++) {
        int filerow = r + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && r == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), 
                "Cedit -- version %s", CEDIT_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;

                // padding is half the remaining cols after subtracting welcome message
                int padding = (E.screencols - welcomelen) / 2;
                // write a tilde to the first col of welcome line
                if (padding) {
                    // ~ at the beginning of line like vim
                    abAppend(ab, "~", 1);
                    padding--;
                }
                // exhaust left padding and append welcome message
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                // ~ at the beginning of line like vim
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        // clear right of line
        // more efficient than screen redraw
        abAppend(ab, "\x1b[K", 3);

        // don't draw on last line
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
        E.filename ? E.filename : "[No Name]", E.numrows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", 
        E.cy + 1, E.numrows);
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorScroll()
{
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;
    // don't use clear screen func here because 
    // write is inneficient on draw

    //hide cursor
    abAppend(&ab, "\x1b[?25l", 6);
    //return cursor to start
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);

    // Move cursor to E.cy+1, E.cx+1
    // (Terminal uses 1 indexed vals)
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy-E.rowoff) + 1, (E.rx - E.coloff)+1);
    abAppend(&ab, buf, strlen(buf));

    //show cursor again
    abAppend(&ab, "\x1b[?25h", 6);

    // write buffer
    write(STDOUT_FILENO, ab.b, ab.len);

    // free buffer
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** init ***/
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    // Initializes vals in E struct

    // set row/col size && die on fail
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}