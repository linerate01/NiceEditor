#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <signal.h> 
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

#define MENU_HEIGHT 1
#define STATUS_HEIGHT 1
#define MAX_FILES 256
#define MAX_ROWS 1500
#define MAX_COLS 256

WINDOW* editor_win;

const char* menu_titles[] = {"File", "Build", "Option", "Help"};
const char* file_menu[] = {"New", "Open", "Save", "Exit"};
const char* build_menu[] = {"Run", "Link"};
const char* option_menu[] = {"NumLine", "Syntax", "Bracket", "AutoSave", "Seconds"};
const char* help_menu[] = {"Status", "Guide"};
char clipboard[MAX_ROWS][MAX_COLS];  // 복사된 라인 저장
int clipboard_line_count = 0;        // 복사된 줄 수
char msg[256] = "";

int show_line_numbers = 0;
int show_syntax_highlight = 1;
int hide_brackets = 0;
int current_menu = -1;
int current_item = 0;
int input_enabled = 0;
int autosave_enabled = 0;
int tick = 5;
int offset = 0; 

int cursor_x = 0, cursor_y = 0;
char buf[MAX_ROWS][MAX_COLS];

char current_filename[256] = "";
char link_flags[256] = "";
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void draw_menubar();
void draw_window();
void draw_status(const char* message);
void draw_dropdown(int menu_index);

void render_buffer();

void keyInput(int ch);
void resize(int sig);
int menuInput(int ch);


int get_filename(char* out_filename);
int cyan(const char* word);
int magenta(const char* word);

void show_logo();
void show_file_list_popup();

void clear_dropdown(int menu_index);

void run();
void tap(int actual_row, int actual_col);
void countBlock(int actual_row, int actual_col);
void search();
void saveFile();

void* saveFileThread(void*);
void autoSaveHandler();

int get_menu_item_count(int menu_index);
void show_help_status_popup();
void show_help_guide_popup();

const char* cyan_keywords[] = {"int", "double", "float", "enum", "char", "short", "long", "malloc", "free", "calloc", "realloc", NULL};
const char* magenta_keywords[] = {"void", "unsigned", "signed", "sizeof", "typedef", "struct", "union", "extern", "static", "const",
                                 "if", "else", "switch", "case", "default", "while", "for", "do", "continue", "break", "return", NULL};

int prompt_for_line(const char* prompt) {
    echo();
    curs_set(1);
    WINDOW* input_win = newwin(3, 40, (LINES - 3) / 2, (COLS - 40) / 2);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 1, "%s", prompt);
    wrefresh(input_win);

    char input[10];
    wgetnstr(input_win, input, 9);
    int line = atoi(input);

    delwin(input_win);
    noecho();
    curs_set(0);
    return line;
}

void show_help_guide_popup() {
    const char* help_text[] = {
        "Nice Editor - Usage Guide",
        "",
        "Ctrl+L         : Open menu",
        "F5             : Compile and run (gcc)",
        "Alt+S          : Save file",
        "Ctrl+F         : Find text",
        "",
        "Arrow Keys     : Move cursor",
        "Enter          : Insert newline",
        "Backspace      : Delete character",
        "TAB            : Insert 4 spaces",
        "",
        "Menu Shortcuts:",
        "  File > New/Open/Save/Exit",
        "  Build > Run/Link",
        "  Option > Toggle settings",
        "  Help > View status or guide",
        "",
        "This editor supports syntax highlighting,",
        "line numbering, bracket hiding, auto-save,",
        "and mouse-less full keyboard navigation.",
        NULL
    };

    int total_lines = 0;
    while (help_text[total_lines]) total_lines++;

    int win_h = 15, win_w = 60;
    int start_y = (LINES - win_h) / 2;
    int start_x = (COLS - win_w) / 2;

    WINDOW* win = newwin(win_h, win_w, start_y, start_x);
    box(win, 0, 0);
    keypad(win, TRUE);

    int offset = 0;
    int ch;

    while (1) {
        werase(win);
        box(win, 0, 0);
        for (int i = 0; i < win_h - 2 && i + offset < total_lines; i++) {
            mvwprintw(win, i + 1, 2, "%s", help_text[i + offset]);
        }
        mvwprintw(win, win_h - 1, 2, "Scroll, ESC to exit");
        wrefresh(win);

        ch = wgetch(win);
        if (ch == 27) break;
        else if (ch == KEY_DOWN && offset + win_h - 2 < total_lines) offset++;
        else if (ch == KEY_UP && offset > 0) offset--;
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    render_buffer();
}

// Ctrl + E
void move_line() {
    char query[256] = "";
    int width = 40;
    int height = 5;
    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;
   // 창 만들어서 띄우기
    WINDOW* input_win = newwin(height, width, start_y, start_x);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 2, "Move to :");
    wrefresh(input_win);

    echo();
    curs_set(1);
    mvwgetnstr(input_win, 2, 2, query, 255);
    noecho();
    curs_set(0);

    delwin(input_win);
    touchwin(stdscr); //부모 윈도우 화면정보 갱신
    refresh();

    if (strlen(query) == 0) {
        draw_status("Move cancelled.");
        return;
    }
    int target_line = atoi(query) - 1; // 사용자가 1부터 시작하므로 -1
    if (target_line < 0 || target_line >= MAX_ROWS) {
        draw_status("Invalid line number.");
        return;
    }
    // 특정 위치로 이동 target line
    cursor_y = target_line; 
    cursor_x = 0; 
    offset = cursor_y; // 스크롤
    if (cursor_y >= MAX_ROWS) cursor_y = MAX_ROWS - 1; // 범위체크
    render_buffer();
    char msg[256];
    snprintf(msg, sizeof(msg), "Moved to line %d", target_line + 1);
    draw_status(msg);
}

void show_help_status_popup() {
    int win_h = 10, win_w = 50;
    int start_y = (LINES - win_h) / 2;
    int start_x = (COLS - win_w) / 2;
    WINDOW* popup = newwin(win_h, win_w, start_y, start_x);
    box(popup, 0, 0);

    mvwprintw(popup, 1, 2, "Current Option Status:");
    mvwprintw(popup, 2, 4, "Numbering Line       : %s", show_line_numbers ? "ON" : "OFF");
    mvwprintw(popup, 3, 4, "Syntax Highlighting  : %s", show_syntax_highlight ? "ON" : "OFF");
    mvwprintw(popup, 4, 4, "Bracket Visibility   : %s", hide_brackets ? "Hidden" : "Shown");
    mvwprintw(popup, 5, 4, "AutoSave             : %s", autosave_enabled ? "ON" : "OFF");
    mvwprintw(popup, 6, 4, "AutoSave Interval    : %d sec", tick);

    mvwprintw(popup, 8, 2, "Press ESC to close...");
    wrefresh(popup);

    keypad(popup, TRUE);
    int ch;
    while ((ch = wgetch(popup)) != 27) {} // ESC
    delwin(popup);
    touchwin(stdscr);
    refresh();
    render_buffer();
}

int get_menu_item_count(int menu_index) {
    if (menu_index == 0) return 4;
    if (menu_index == 1) return 2;
    if (menu_index == 2) return 5; // NUM, SYN, Bracket, AutoSave, Seconds
    if (menu_index == 3) return 2;
    return 0;
}

// autosave = 1 인 경우에, signal을 통해서 이 함수가 불린다.
void autoSaveHandler() { 
    if (!autosave_enabled) return;
    pthread_t t;

    if (pthread_create(&t, NULL, saveFileThread, NULL) != 0) 
        perror("can't creat thread for autosave");
    else
        pthread_detach(t); // t 할 일 끝나고 삭제

    alarm(tick); //기본값 = 5, 이후에 수정하면 바뀜.
}

void* saveFileThread(void *arg) {
    pthread_mutex_lock(&mutex);
    const char* filename = current_filename;
    FILE* fp = fopen(filename, "w"); 
   // 덮어쓰기함. 기존 사용자가 어느 부분을 수정했을지 알 수 없음
    if (!fp) {
        draw_status("Failed to save file.");
        pthread_mutex_unlock(&mutex);
        return NULL;
    }
    for (int i = 0; i < MAX_ROWS; i++) {
        fprintf(fp, "%s\n", buf[i]);
    }
    fclose(fp);
    draw_status("Autosaved.");
    pthread_mutex_unlock(&mutex);
    return NULL;
}

int get_user_input(const char* prompt, char* out) {
    int width = 50, height = 5;
    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;

    WINDOW* win = newwin(height, width, start_y, start_x);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "%s", prompt);
    wrefresh(win);

    echo();
    curs_set(1);
    mvwgetnstr(win, 2, 2, out, 255);
    noecho();
    curs_set(0);

    delwin(win);
    touchwin(stdscr);
    refresh();
    return strlen(out) > 0;
}

// 텍스트 찾는 함수
// 기본 틀로 입력받고 첫 줄부터 쭉 내려가면서 포함되어 있는지 확인
// 포함되어 있다면 col을 찾아서 row, col 알려줌

void search() {
    char query[256] = "";
    int width = 40;
    int height = 5;
    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;

    WINDOW* input_win = newwin(height, width, start_y, start_x);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 2, "Search for:");
    wrefresh(input_win);

    echo();
    curs_set(1);
    mvwgetnstr(input_win, 2, 2, query, 255);
    noecho();
    curs_set(0);

    delwin(input_win);
    touchwin(stdscr);
    refresh();

    if (strlen(query) == 0) {
        draw_status("Search cancelled.");
        return;
    }

    // 검색 수행
    for (int i = 0; i < MAX_ROWS; i++) {
        char* pos = strstr(buf[i], query);
        if (pos) {
            int col = pos - buf[i];
            cursor_y = 0;
            offset = i;
            cursor_x = col;
            render_buffer();

            char msg[256];
            snprintf(msg, sizeof(msg), "Found at line %d, column %d", i + 1, col + 1);
            draw_status(msg);
            return;
        }
    }

    draw_status("No match found.");
}

// 컴파일 함수
// gnome-terminal을 이용한 새로운 창을 만들고 —bash 로 스크립트 실행
// 자식프로세스가 명령어로 실행함
// 부모는 기다린다. 보통 사용자가 코드 실행 후 화면을 볼 것이라 판단.

void run() {
    if (strlen(current_filename) == 0) {
        draw_status("No file to compile.");
        return;
    }

    saveFile();  // 현재 파일 저장

    char cmd[1024];
   snprintf(cmd, sizeof(cmd),
    "gnome-terminal -- bash -c \"gcc '%s' %s -o temp_out && ./temp_out; echo; echo Press ENTER to close...; read\"",
    current_filename,
    link_flags);

    pid_t pid = fork();
    if (pid == 0) {
        // 자식 프로세스가 명령 실행
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        exit(1);  // execl 실패 시 종료
    } else if (pid < 0) {
        draw_status("Failed to fork.");
    } else {
        waitpid(pid, NULL, 0);  // 안전하게 자식 종료 기다리기
    }
}

int cyan(const char* word) {
    for (int i = 0; cyan_keywords[i] != NULL; i++) {
        if (strcmp(word, cyan_keywords[i]) == 0) return 1;
    }
    return 0;
}

int magenta(const char* word) {
    for (int i = 0; magenta_keywords[i] != NULL; i++) {
        if (strcmp(word, magenta_keywords[i]) == 0) return 1;
    }
    return 0;
}

void show_logo() {
    int unused_rows, cols;
    (void)unused_rows;
    getmaxyx(stdscr, unused_rows, cols);

    const char* logo[] = {
        " _   _  _               _____      _  _  _                ",
        "| \\ | |(_)             |  ___|    | |(_)| |               ",
        "|  \\| | _   ___   ___  | |__    __| | _ | |_   ___   _ __ ",
        "| . ` || | / __| / _ \\ |  __|  / _` || || __| / _ \\ | '__|",
        "| |\\  || || (__ |  __/ | |___ | (_| || || |_ | (_) || |   ",
        "\\_| \\_/|_| \\___| \\___| \\____/  \\__,_||_| \\__| \\___/ |_|   "
    };

    int logo_lines = sizeof(logo) / sizeof(logo[0]);
    int win_height = getmaxy(editor_win) - 2;
    int start_y = (win_height - logo_lines) / 2;

    werase(editor_win);
    box(editor_win, 0, 0);

    wattron(editor_win, COLOR_PAIR(4));  // 마젠타 색상 ON

    for (int i = 0; i < logo_lines; i++) {
        int len = strlen(logo[i]);
        int start_x = (cols - len) / 2;
        mvwprintw(editor_win, start_y + i, start_x, "%s", logo[i]);
    }

    wattroff(editor_win, COLOR_PAIR(4)); // 색상 OFF
    wrefresh(editor_win);

    usleep(1500000);  // 1.5초 대기
    werase(editor_win);
    box(editor_win, 0, 0);
    wrefresh(editor_win);
}


void draw_menubar() {
    int cols = getmaxx(stdscr); //가로 축 최대 길이
    const char* menu_text = " File  Build  Option  Help ";
    const char* hint = "Ctrl+L: menu | F10: exit | F5: compile & run | Ctrl+F: find";

    // 메뉴바 왼쪽
    attron(COLOR_PAIR(2));
    mvprintw(0, 0, "%s", menu_text);
    attroff(COLOR_PAIR(2));

    // 오른쪽 여백에 단축키 힌트 출력 (오른쪽 정렬)
    int hint_start = cols - strlen(hint) - 1;
    if (hint_start > (int)strlen(menu_text)) {
        mvprintw(0, hint_start, "%s", hint);
    }

    refresh();
}

void draw_window() {
    if (editor_win) {
        delwin(editor_win);
    }
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    editor_win = newwin(rows - MENU_HEIGHT - STATUS_HEIGHT, cols, MENU_HEIGHT, 0);
    box(editor_win, 0, 0);
    wrefresh(editor_win);
}

// 상태창 출력
void draw_status(const char* message) {
    if (message) strncpy(msg, message, sizeof(msg));
    
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    char left[cols + 1];
    char right[cols + 1];

    snprintf(left, sizeof(left), " %s", msg);
    snprintf(right, sizeof(right), " %s | Ln %d, Col %d ", 
             (strlen(current_filename) > 0) ? current_filename : "[No File]",
             cursor_y + offset + 1, cursor_x + 1);

    int left_len = strlen(left);
    int right_len = strlen(right);
    int space = cols - left_len - right_len;
    if (space < 0) space = 0;

    move(rows - 1, 0);
    clrtoeol();
    attron(A_REVERSE);
    printw("%s%*s%s", left, space, "", right);
    attroff(A_REVERSE);
    refresh();
}

// buffer 출력하기
void render_buffer() {
    werase(editor_win);
    box(editor_win, 0, 0);
    int visible_lines = getmaxy(editor_win) - 2;
    int gutter = show_line_numbers ? 4 : 0;
    pthread_mutex_lock(&mutex);
    for (int y = 0; y < visible_lines; y++) {
        int buf_line = y + offset;
        if (buf_line >= MAX_ROWS) continue;

        const char* line = buf[buf_line];
        int len = strlen(line);
        int x = gutter + 1;

        if (show_line_numbers) {
            mvwprintw(editor_win, y + 1, 1, "%3d", buf_line + 1);
            mvwaddch(editor_win, y + 1, 4, ACS_VLINE);
        }

        for (int i = 0; i < len;) {
            if (hide_brackets && (line[i] == '{' || line[i] == '}' || line[i] == '(' || line[i] == ')')) {
                i++; x++;
                continue;
            }

            if (show_syntax_highlight && line[i] == '\"') {
                wattron(editor_win, COLOR_PAIR(5));
                mvwaddch(editor_win, y + 1, x++, line[i++]);
                while (i < len && !(line[i] == '\"' && line[i - 1] != '\\')) {
                    mvwaddch(editor_win, y + 1, x++, line[i++]);
                }
                if (i < len) mvwaddch(editor_win, y + 1, x++, line[i++]);
                wattroff(editor_win, COLOR_PAIR(5));
            } else if (show_syntax_highlight && i == 0 && line[i] == '#') {
                wattron(editor_win, COLOR_PAIR(5));
                while (i < len) mvwaddch(editor_win, y + 1, x++, line[i++]);
                wattroff(editor_win, COLOR_PAIR(5));
                break;
            } else if (show_syntax_highlight && line[i] == '/' && line[i + 1] == '/') {
                wattron(editor_win, COLOR_PAIR(6));
                while (i < len) mvwaddch(editor_win, y + 1, x++, line[i++]);
                wattroff(editor_win, COLOR_PAIR(6));
                break;
            } else if (line[i] == ';') {
                if (show_syntax_highlight) wattron(editor_win, COLOR_PAIR(7));
                mvwaddch(editor_win, y + 1, x++, line[i++]);
                if (show_syntax_highlight) wattroff(editor_win, COLOR_PAIR(7));
            } else if (isalpha(line[i]) || line[i] == '_') {
                char word[64] = {0};
                int j = 0;
                while ((isalnum(line[i]) || line[i] == '_') && j < 63) {
                    word[j++] = line[i++];
                }
                word[j] = '\0';

                if (show_syntax_highlight && cyan(word)) {
                    wattron(editor_win, COLOR_PAIR(9));
                    mvwprintw(editor_win, y + 1, x, "%s", word);
                    wattroff(editor_win, COLOR_PAIR(9));
                } else if (show_syntax_highlight && magenta(word)) {
                    wattron(editor_win, COLOR_PAIR(8));
                    mvwprintw(editor_win, y + 1, x, "%s", word);
                    wattroff(editor_win, COLOR_PAIR(8));
                } else {
                    mvwprintw(editor_win, y + 1, x, "%s", word);
                }
                x += strlen(word);
            } else {
                mvwaddch(editor_win, y + 1, x++, line[i++]);
            }
        }
        pthread_mutex_unlock(&mutex);
        if (y == cursor_y) {
            wmove(editor_win, y + 1, cursor_x);
        }
    }
    int max_y = getmaxy(editor_win) - 2;
    int max_x = getmaxx(editor_win) - 2;
    if (cursor_y >= max_y) cursor_y = max_y - 1;
    if (cursor_x >= max_x) cursor_x = max_x - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x < 0) cursor_x = 0;
    wmove(editor_win, cursor_y + 1, cursor_x + 1);
    curs_set(1);
    wrefresh(editor_win);
}

// 현재 파일 저장
void saveFile() {
    pthread_mutex_lock(&mutex);
    const char* filename = (strlen(current_filename) > 0) ? current_filename : "untitled.txt";
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        draw_status("Failed to save file.");
        return;
    }
    for (int i = 0; i < MAX_ROWS; i++) {
        fprintf(fp, "%s\n", buf[i]);
    }
    fclose(fp);
    draw_status("Saved to file.");
    pthread_mutex_unlock(&mutex);
}

// 탭기능 구현
void tap(int actual_row, int actual_col){
    if (strlen(buf[actual_row]) + 4 < MAX_COLS - 1) {
        for (int i = 0; i < 4; i++) {
            memmove(&buf[actual_row][actual_col + 1],
                    &buf[actual_row][actual_col],
                    strlen(&buf[actual_row][actual_col]) + 1);
            buf[actual_row][actual_col++] = ' ';
            cursor_x++;
        }
    }
}

// 단순하게 처음부터 { 나오면 +, } 는 - 해서 count만큼 탭한다.
void countBlock(int actual_row, int actual_col){
    int count = 0;
    for(int i = 0; i <= actual_row; i++){
        if(strchr(buf[i], '{'))
            count++;
        if(strchr(buf[i], '}'))
            count--;
    }

    for(int i = 0; i < count; i++){
        tap(actual_row, actual_col);
    }
}

void keyInput(int ch) {
    if (!input_enabled || !editor_win) return;
    int visible_lines = getmaxy(editor_win) - 2;
    int actual_row = cursor_y + offset;
    int gutter = show_line_numbers ? 4 : 0;
    int actual_col = cursor_x - gutter;

    switch (ch) {
        case KEY_LEFT: // 왼쪽으로 이동
            if (cursor_x > gutter) {
                cursor_x--;
            } else if (cursor_y > 0 || offset > 0) {
                // 이전 줄의 끝으로 이동
                if (cursor_y > 0) {
                    cursor_y--;
                } else {
                    offset--;
                }
                actual_row = cursor_y + offset;
                cursor_x = strlen(buf[actual_row]) + gutter;
            }
            break;

        case KEY_RIGHT: // 오른쪽으로 이동
            if (buf[actual_row][actual_col] != '\0' && cursor_x < MAX_COLS - 2) {
                cursor_x++;
            } else if (cursor_y < visible_lines - 1 && actual_row + 1 < MAX_ROWS) {
                cursor_y++;
                cursor_x = gutter;
            } else if (offset + visible_lines < MAX_ROWS) {
                offset++;
                cursor_x = gutter;
            }
            break;

        case KEY_UP: // 위로 이동
            if (cursor_y > 0) {
                cursor_y--;
            } else if (offset > 0) {
                offset--;
            }
            //그 줄의 뒤로 가도록 계산 
            actual_row = cursor_y + offset;
            cursor_x = strlen(buf[actual_row]) + gutter;
            break;

        case KEY_DOWN:
            if (cursor_y < visible_lines - 1 && actual_row + 1 < MAX_ROWS) {
                cursor_y++;
            } else if (offset +
                visible_lines < MAX_ROWS) {
                offset++;
            }
            //그 줄의 뒤로 가도록 계산 
            actual_row = cursor_y + offset;
            cursor_x = strlen(buf[actual_row]) + gutter;
            break;

        case KEY_BACKSPACE:
        case 127:
        case 8:
            pthread_mutex_lock(&mutex);
            if (cursor_x > gutter) {
                memmove(&buf[actual_row][actual_col - 1],
                        &buf[actual_row][actual_col],
                        strlen(&buf[actual_row][actual_col]) + 1);
                cursor_x--;
            } else if (actual_row > 0) {
                int prev_len = strlen(buf[actual_row - 1]);
                if (prev_len + strlen(buf[actual_row]) < MAX_COLS) {

                    strcat(buf[actual_row - 1], buf[actual_row]);

                    for (int i = actual_row; i < MAX_ROWS - 1; i++) {
                        strcpy(buf[i], buf[i + 1]);
                    }

                    if (cursor_y > 0) {
                        cursor_y--;
                    } else {
                        offset--;
                    }
                    actual_row = cursor_y + offset;
                    cursor_x = prev_len + gutter;
                }
            }
            pthread_mutex_unlock(&mutex);
            break;

        case 10:  // 엔터
            pthread_mutex_lock(&mutex);
            if(buf[actual_row][actual_col - 1] == '{' && buf[actual_row][actual_col] == '}'
                && actual_row < MAX_ROWS - 2){
                    for(int k = 0; k < 2; k++){
                        for (int i = MAX_ROWS - 2; i > actual_row; i--) {
                            strcpy(buf[i + 1], buf[i]);
                        }
                        strcpy(buf[actual_row + 1], &buf[actual_row][actual_col]);
                        buf[actual_row][actual_col] = '\0';

                        if (cursor_y < visible_lines - 1) {
                            cursor_y++;
                        } else {
                            offset++;
                        }
                        cursor_x = gutter;
                        actual_col = 0;
                        actual_row = cursor_y + offset;
                    }
                    countBlock(actual_row, actual_col);
                    cursor_y--;
                    cursor_x = gutter;
                    actual_col = 0;
                    actual_row = cursor_y + offset;
                }
            else if (actual_row < MAX_ROWS - 1) {
                for (int i = MAX_ROWS - 2; i > actual_row; i--) {
                    strcpy(buf[i + 1], buf[i]);
                }
                strcpy(buf[actual_row + 1], &buf[actual_row][actual_col]);
                buf[actual_row][actual_col] = '\0';

                if (cursor_y < visible_lines - 1) {
                    cursor_y++;
                } else {
                    offset++;
                }
                cursor_x = gutter;
                actual_col = 0;
                actual_row = cursor_y + offset;
            }
            countBlock(actual_row, actual_col);
            pthread_mutex_unlock(&mutex);
            break;
        case 27: { // ESC = Alt
            int next = getch();

            if (next == 'c' || next == 'C') {
                // Alt+C: 복사
                int start = prompt_for_line("Start line to copy:") - 1;
                int end = prompt_for_line("End line to copy:") - 1;

                if (start >= 0 && end >= start && end < MAX_ROWS) {
                    clipboard_line_count = 0;
                    for (int i = start; i <= end; ++i)
                        strncpy(clipboard[clipboard_line_count++], buf[i], MAX_COLS);

                    snprintf(msg, sizeof(msg), "Copied %d lines", clipboard_line_count);
                } else {
                    snprintf(msg, sizeof(msg), "Invalid line range");
                }

            } else if (next == 'v' || next == 'V') {
                // Alt+V: 붙
                if (clipboard_line_count == 0) {
                    snprintf(msg, sizeof(msg), "Clipboard is empty");
                    break;
                }

                int dest = prompt_for_line("Target line to paste at:") - 1;
                if (dest < 0 || dest >= MAX_ROWS) {
                    snprintf(msg, sizeof(msg), "Invalid target line");
                    break;
                }

                // 클립보드 줄 수 만큼 아래로 밀기
                for (int i = MAX_ROWS - 1; i >= dest + clipboard_line_count; --i)
                    strncpy(buf[i], buf[i - clipboard_line_count], MAX_COLS);

                // 클립보드버퍼로부터 복사하기
                for (int i = 0; i < clipboard_line_count; ++i)
                    strncpy(buf[dest + i], clipboard[i], MAX_COLS);

                snprintf(msg, sizeof(msg), "Pasted %d lines at line %d", clipboard_line_count, dest + 1);
            }

            break;
        }

        default:
            if (ch == '\t') {
                // 탭 키 입력 시 공백 4칸 삽입
                tap(actual_row, actual_col);
            } else if (ch >= 32 && ch <= 126 && strlen(buf[actual_row]) < MAX_COLS - 1) {
                pthread_mutex_lock(&mutex);
                if(ch == '{') {
                    memmove(&buf[actual_row][actual_col + 2],
                            &buf[actual_row][actual_col],
                            strlen(&buf[actual_row][actual_col]) + 2);
                    buf[actual_row][actual_col++] = '{';
                    buf[actual_row][actual_col] = '}';
                    cursor_x += 1;
                }
                else if(ch == '(') {
                    memmove(&buf[actual_row][actual_col + 2],
                            &buf[actual_row][actual_col],
                            strlen(&buf[actual_row][actual_col]) + 2);
                    buf[actual_row][actual_col++] = '(';
                    buf[actual_row][actual_col] = ')';
                    cursor_x += 1;
                }
                else if(ch == '\"' || ch == '\'') {
                    memmove(&buf[actual_row][actual_col + 2],
                            &buf[actual_row][actual_col],
                            strlen(&buf[actual_row][actual_col]) + 2);
                    buf[actual_row][actual_col++] = ch;
                    buf[actual_row][actual_col] = ch;
                    cursor_x += 1;
                }
                else{
                    memmove(&buf[actual_row][actual_col + 1],
                            &buf[actual_row][actual_col],
                            strlen(&buf[actual_row][actual_col]) + 1);
                            buf[actual_row][actual_col] = ch;
                            cursor_x++;
                }
                pthread_mutex_unlock(&mutex);
            }
            break;
        
    }
    int line_len = strlen(buf[actual_row]);
    if (actual_col > line_len) cursor_x = line_len + gutter;
    
    draw_status(NULL);
    render_buffer();
}


void show_file_list_popup() {
    DIR* dir;
    struct dirent* entry;
    char* files[MAX_FILES];
    int count = 0;

    dir = opendir(".");
    if (!dir) {
        draw_status("Failed to open directory.");
        return;
    }

    while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
        if (entry->d_type == DT_REG) {
            files[count] = strdup(entry->d_name);
            count++;
        }
    }
    closedir(dir);

    int win_h = 15;  // 고정된 높이로 보여줄 최대 줄 수 + 테두리
    int win_w = 40;
    int start_y = 3;
    int start_x = 5;

    WINDOW* popup = newwin(win_h, win_w, start_y, start_x);
    box(popup, 0, 0);
    mvwprintw(popup, 1, 2, "Select a file:");

    int highlight = 0;
    int offset = 0;  // 스크롤 오프셋
    int ch;
    keypad(popup, TRUE);

    while (1) { 
        for (int i = 0; i < win_h - 4; i++) {
            int file_index = i + offset;
            if (file_index >= count) break;

            if (file_index == highlight)
                wattron(popup, A_REVERSE);
            mvwprintw(popup, i + 2, 2, "%-36s", files[file_index]);
            wattroff(popup, A_REVERSE);
        }
        wrefresh(popup);

        ch = wgetch(popup);
        if (ch == KEY_UP) {
            if (highlight > 0) highlight--;
            if (highlight < offset) offset--;
        } else if (ch == KEY_DOWN) {
            if (highlight < count - 1) highlight++;
            if (highlight >= offset + (win_h - 4)) offset++;
        } else if (ch == 10) {
            FILE* fp = fopen(files[highlight], "r");
            if (fp) {
                memset(buf, 0, sizeof(buf));
                int row = 0;
                while (fgets(buf[row], MAX_COLS, fp) && row < MAX_ROWS - 1) {
                    buf[row][strcspn(buf[row], "\n")] = '\0';
                    row++;
                }
                fclose(fp);
                strcpy(current_filename, files[highlight]);
                input_enabled = 1;
                cursor_x = cursor_y = 0;
                render_buffer();
            } else {
                draw_status("Failed to open file.");
            }
            break;
        } else if (ch == 27) {
            break;
        }
    }

    for (int i = 0; i < count; i++) free(files[i]);
    delwin(popup);
    touchwin(stdscr);
    refresh();
    box(editor_win, 0, 0);
    wrefresh(editor_win);
}

void draw_dropdown(int menu_index) {
    int x = menu_index * 6;
    const char** menu_items;
    int count = 0;

    if (menu_index == 0) {
        menu_items = file_menu;
        count = 4;
    } else if (menu_index == 1) {
        menu_items = build_menu;
        count = 2;
    } else if (menu_index == 2) {
        menu_items = option_menu;
        count = 5;
    } else if (menu_index == 3) {
        menu_items = help_menu;
        count = 2;
    }else {
        return;
    }

    for (int i = 0; i < count; i++) {
        attron(COLOR_PAIR(i == current_item ? 2 : 1));
        mvprintw(1 + i, x, "%-10s", menu_items[i]);
        attroff(COLOR_PAIR(i == current_item ? 2 : 1));
    }
    refresh();
}

void clear_dropdown(int menu_index) {
    int x = menu_index * 6;
    for (int i = 0; i < 4; i++) {
        move(1 + i, x);
        clrtoeol();
    }
    if (editor_win) {
        box(editor_win, 0, 0);
        wrefresh(editor_win);
    }
    refresh();
}

int menuInput(int ch) {
    if (current_menu == -1 && ch == 12) {
        current_menu = 0;
        current_item = 0;
        draw_dropdown(current_menu);
        return 1;
    }

    if (current_menu != -1) {
        switch (ch) {
            case KEY_LEFT:
                clear_dropdown(current_menu);
                current_menu = (current_menu + 3) % 4;
                current_item = 0;
                draw_dropdown(current_menu);
                break;
            case KEY_RIGHT:
                clear_dropdown(current_menu);
                current_menu = (current_menu + 1) % 4;
                current_item = 0;
                draw_dropdown(current_menu);
                break;
            case KEY_UP: {
                int count = get_menu_item_count(current_menu);
                current_item = (current_item + count - 1) % count;
                draw_dropdown(current_menu);
                break;
            }
            case KEY_DOWN: {
                int count = get_menu_item_count(current_menu);
                current_item = (current_item + 1) % count;
                draw_dropdown(current_menu);
                break;
            }
            case 10: // 엔터
                clear_dropdown(current_menu);
                box(editor_win, 0, 0);
                wrefresh(editor_win);

                if (current_menu == 0) { // 파일
                    draw_status(file_menu[current_item]);
                    if (strcmp(file_menu[current_item], "New") == 0) {
                        char newname[256] = "";
                        if (get_filename(newname)) {
                            memset(buf, 0, sizeof(buf));
                            cursor_x = cursor_y = offset = 0;
                            strcpy(current_filename, newname);
                            input_enabled = 1;
                            render_buffer();
                            draw_status("New file created. Start typing.");
                        } else {
                            draw_status("Filename input cancelled.");
                        }
                    } else if (strcmp(file_menu[current_item], "Open") == 0) {
                        show_file_list_popup();
                    } else if (strcmp(file_menu[current_item], "Save") == 0) {
                        saveFile();
                    } else if (strcmp(file_menu[current_item], "Exit") == 0) {
                        saveFile();
                        endwin();
                        exit(0);
                    }
                } else if (current_menu == 1) { // Build
                        const char* item = build_menu[current_item];
                        if (strcmp(item, "Run") == 0) {
                            run();
                        } else if (strcmp(item, "Link") == 0) {
                            char input[256] = "";
                            if (get_user_input("Enter link flags:", input)) {
                                strncpy(link_flags, input, sizeof(link_flags));
                                draw_status("Link flags updated. Running...");
                                run();  // 바로 실행
                            } else {
                                draw_status("Link input cancelled.");
                            }
                        }
                    } else if (current_menu == 2) { // Option
                    const char* item = option_menu[current_item];
                    if (strcmp(item, "NumLine") == 0) {
                        show_line_numbers = !show_line_numbers;
                        draw_status(show_line_numbers ? "Line numbers ON" : "Line numbers OFF");
                    } else if (strcmp(item, "Syntax") == 0) {
                        show_syntax_highlight = !show_syntax_highlight;
                        draw_status(show_syntax_highlight ? "Syntax highlight ON" : "Syntax highlight OFF");
                    } else if (strcmp(item, "Bracket") == 0) {
                        hide_brackets = !hide_brackets;
                        draw_status(hide_brackets ? "Brackets hidden" : "Brackets shown");
                    } else if (strcmp(item, "AutoSave") == 0) {
                        autosave_enabled = !autosave_enabled;
                        draw_status(autosave_enabled ? "AutoSave ON" : "AutoSave OFF");
                        if (autosave_enabled) {
                            alarm(5);  // 시작
                        } else {
                            alarm(0);  // 해제
                        }
                    }else if (strcmp(item, "Seconds") == 0) {
                        char input[256] = "";
                        if (get_user_input("Enter autosave interval (sec):", input)) {
                            int t = atoi(input);
                            if (t >= 1 && t <= 3600) {  // 1초 ~ 1시간 범위 제한
                                tick = t;
                                draw_status("AutoSave interval updated.");
                                if (autosave_enabled) {
                                    alarm(tick);  // 즉시 재설정
                                }
                            } else {
                                draw_status("Invalid interval value (1–3600 sec only).");
                            }
                        } else {
                            draw_status("Interval input cancelled.");
                        }
                    }
                    render_buffer();
                } else if (current_menu == 3) { // Help
                    const char* item = (current_item == 0) ? "Status" : "Guide";
                    if (strcmp(item, "Status") == 0) {
                        show_help_status_popup();
                    } else if (strcmp(item, "Guide") == 0) {
                        show_help_guide_popup();
                    }
                }

                current_menu = -1;
                return 1;
            case 27: // ESC
                clear_dropdown(current_menu);
                box(editor_win, 0, 0);
                wrefresh(editor_win);
                current_menu = -1;
                return 1;
        }
        return 1;
    }
    return 0;
}


void resize(int sig) {
    // ncurses 내부 화면 크기 갱신
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    resizeterm(rows, cols);
    clear();  // stdscr 지우기

    // 에디터 윈도우 재생성
    draw_window();

    // 전체 다시 그리기
    draw_menubar();
    render_buffer();
    draw_status("Window resized.");

    // 시그널 다시 등록
    signal(SIGWINCH, resize);
}

int get_filename(char* out_filename) {
    int width = 40;
    int height = 5;
    int start_y = (LINES - height) / 2;
    int start_x = (COLS - width) / 2;

    WINDOW* input_win = newwin(height, width, start_y, start_x);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 2, "Enter new filename:");
    wrefresh(input_win);

    echo();
    curs_set(1);
    mvwgetnstr(input_win, 2, 2, out_filename, 255);
    noecho();
    curs_set(0);

    delwin(input_win);
    touchwin(stdscr);
    refresh();
    return strlen(out_filename) > 0;
}



int main() {
    
    initscr();
    set_escdelay(25);  // 25ms로 줄임
    signal(SIGWINCH, resize);
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(1);
    
    start_color();
    use_default_colors();

    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    init_pair(2, COLOR_WHITE, COLOR_MAGENTA);
    init_pair(4, COLOR_MAGENTA, -1);
    init_pair(5, COLOR_YELLOW, -1);   // 문자열
    init_pair(6, COLOR_GREEN, -1);    // 주석
    init_pair(7, COLOR_RED, -1);      // 세미콜론
    init_pair(8, COLOR_MAGENTA, -1);  // 마젠타 키워드
    init_pair(9, COLOR_CYAN, -1);     // 시안 키워드

    draw_menubar();
    draw_window();
    show_logo();     // 로고 출력 후 키 입력 대기
    render_buffer(); // 편집기 초기화
    
    signal(SIGALRM, autoSaveHandler);
    draw_status("Welcome to the Nice editor! Press F1 to Guide.");
    int ch;
    while ((ch = getch()) != KEY_F(10)) {
        // Alt+S 입력 감지: ESC → 's'
        if (ch == 27) { // ESC
            if (current_menu != -1) {
                // 드롭다운 메뉴가 열려 있다면 닫기만 하고 다음 키 안 받음
                clear_dropdown(current_menu);
                box(editor_win, 0, 0);
                wrefresh(editor_win);
                current_menu = -1;
                continue;
            }

            // Alt + ?
            int next = getch(); 
            if (next == 's' || next == 'S') { 
                saveFile();
                continue;
            }
            ungetch(next); // 아니면 다음 키
        }
            if (ch == KEY_F(5)) {
            run();
            continue;
        }
        if(ch==KEY_F(1)){
            show_help_guide_popup();
            continue;
        }
   // ctrl + F
        if (ch == 6) { 
            search();
            continue;
        }
   // ctrl + E
        if(ch == 5) { 
            move_line();
            continue;
        }

        if (!menuInput(ch)) {
            keyInput(ch);
        }
    }


    endwin();
    return 0;
}
