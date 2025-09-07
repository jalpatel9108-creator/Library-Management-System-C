// Library Management System
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
  #include <conio.h>
#else
  #include <termios.h>
  #include <unistd.h>
  static int getch(void) {
      struct termios oldt, newt;
      int ch;
      tcgetattr(STDIN_FILENO, &oldt);
      newt = oldt;
      newt.c_lflag &= ~(ICANON | ECHO);
      tcsetattr(STDIN_FILENO, TCSANOW, &newt);
      ch = getchar();
      tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
      return ch;
  }
#endif

#define DATA_FILE     "books.dat"
#define STUDENT_FILE  "students.dat"
#define ISSUE_FILE    "issues.dat"
#define ADMIN_CFG     "admin.cfg"
#define DEFAULT_ADMIN_PASS "admin123"
#define FINE_PER_DAY 5

// Models
struct Book {
    int id;
    char title[100];
    char author[100];
    int available; 
};

struct Student {
    int id;
    char name[100];
};

struct Issue {
    int book_id;
    int student_id;
    time_t issue_time;
    int due_days;
    int returned;     
    time_t return_time;
};


static void pauseForUser(void) {
    printf("\nPress Enter to continue...");
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}


static void safeLocalTime(struct tm *out, const time_t *t) {
    struct tm *tmp = localtime(t);
    if (tmp) *out = *tmp;
    else memset(out, 0, sizeof(*out));
}


static void readLineSafe(char *buf, size_t sz) {
    if (!fgets(buf, (int)sz, stdin)) { buf[0] = 0; return; }
    buf[strcspn(buf, "\n")] = 0;
}

static int readInt(int *out) {
    char line[128];
    if (!fgets(line, sizeof(line), stdin)) return 0;
    char *endptr;
    errno = 0;
    long val = strtol(line, &endptr, 10);
    if (errno != 0 || endptr == line || (*endptr != '\n' && *endptr != '\0')) return 0;
    *out = (int)val;
    return 1;
}

static int readIntWithPrompt(const char *prompt, int defaultVal) {
    printf("%s", prompt);
    int v;
    if (readInt(&v)) return v;
    printf("Invalid input. Using default %d.\n", defaultVal);
    return defaultVal;
}
static void readPasswordMasked(char *out, size_t sz) {
    size_t i = 0;
    while (1) {
        int ch = getch();
        if (ch == 13 || ch == '\n') { out[i] = '\0'; printf("\n"); return; }
        if (ch == 8 || ch == 127) { if (i>0) { i--; printf("\b \b"); } }
        else if (ch >= 32 && ch <= 126) { if (i + 1 < sz) { out[i++] = (char)ch; printf("*"); } }
    }
}
static void ensureDataFilesExist(void) {
    FILE *f;
    f = fopen(DATA_FILE, "ab"); if (f) fclose(f);
    f = fopen(STUDENT_FILE, "ab"); if (f) fclose(f);
    f = fopen(ISSUE_FILE, "ab"); if (f) fclose(f);
}
static int adminPasswordRead(char *buf, int size) {
    FILE *f = fopen(ADMIN_CFG, "r");
    if (!f) {
        f = fopen(ADMIN_CFG, "w");
        if (!f) return 0;
        fprintf(f, "%s", DEFAULT_ADMIN_PASS);
        fclose(f);
        strncpy(buf, DEFAULT_ADMIN_PASS, size-1);
        buf[size-1] = 0;
        return 1;
    }
    if (!fgets(buf, size, f)) { fclose(f); return 0; }
    buf[strcspn(buf, "\n")] = 0;
    fclose(f);
    return 1;
}

static int adminLogin(void) {
    char correct[128];
    if (!adminPasswordRead(correct, sizeof(correct))) {
        printf("Error reading admin password.\n");
        return 0;
    }
    for (int attempt = 1; attempt <= 3; attempt++) {
        char pass[128];
        printf("Enter admin password (Attempt %d/3): ", attempt);
        readPasswordMasked(pass, sizeof(pass));
        if (strcmp(pass, correct) == 0) return 1;
        printf("Incorrect password.\n");
    }
    return 0;
}

static void changeAdminPassword(void) {
    char p1[128], p2[128];
    printf("Enter new password: ");
    readPasswordMasked(p1, sizeof(p1));
    printf("Confirm new password: ");
    readPasswordMasked(p2, sizeof(p2));
    if (strcmp(p1, p2) != 0) { printf("Passwords do not match.\n"); return; }
    FILE *f = fopen(ADMIN_CFG, "w");
    if (!f) { printf("Unable to save password.\n"); return; }
    fprintf(f, "%s", p1);
    fclose(f);
    printf("Admin password changed.\n");
}
static void resetAdminPasswordToDefault(void) {
    FILE *f = fopen(ADMIN_CFG, "w");
    if (!f) { printf("Unable to reset admin password.\n"); return; }
    fprintf(f, "%s", DEFAULT_ADMIN_PASS);
    fclose(f);
    printf("Admin password reset to default '%s'.\n", DEFAULT_ADMIN_PASS);
}
static int studentExists(int id, char *nameBuf) {
    FILE *f = fopen(STUDENT_FILE, "rb");
    if (!f) return 0;
    struct Student s;
    while (fread(&s, sizeof(s), 1, f)) {
        if (s.id == id) {
            if (nameBuf) strncpy(nameBuf, s.name, 99);
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int studentIdDuplicate(int id) {
    return studentExists(id, NULL);
}
static int bookExists(int id, struct Book *out) {
    FILE *f = fopen(DATA_FILE, "rb");
    if (!f) return 0;
    struct Book b;
    while (fread(&b, sizeof(b), 1, f)) {
        if (b.id == id) {
            if (out) *out = b;
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int bookIdDuplicate(int id) {
    return bookExists(id, NULL);
}
static int bookIsIssued(int book_id) {
    FILE *fi = fopen(ISSUE_FILE, "rb");
    if (!fi) return 0;
    struct Issue iss;
    while (fread(&iss, sizeof(iss), 1, fi)) {
        if (iss.book_id == book_id && !iss.returned) { fclose(fi); return 1; }
    }
    fclose(fi);
    return 0;
}
static int studentHasUnreturned(int student_id) {
    FILE *fi = fopen(ISSUE_FILE, "rb");
    if (!fi) return 0;
    struct Issue iss;
    while (fread(&iss, sizeof(iss), 1, fi)) {
        if (iss.student_id == student_id && !iss.returned) { fclose(fi); return 1; }
    }
    fclose(fi);
    return 0;
}
static void addBook(void) {
    printf("Enter Book ID: ");
    int id;
    if (!readInt(&id)) { printf("Invalid ID.\n"); return; }
    if (id <= 0) { printf("ID must be positive.\n"); return; }
    if (bookIdDuplicate(id)) { printf("Book ID already exists.\n"); return; }
    struct Book b;
    b.id = id;
    getchar(); 
    printf("Enter Title: "); readLineSafe(b.title, sizeof(b.title));
    printf("Enter Author: "); readLineSafe(b.author, sizeof(b.author));
    b.available = 1;
    FILE *f = fopen(DATA_FILE, "ab");
    if (!f) { printf("Unable to open books file.\n"); return; }
    fwrite(&b, sizeof(b), 1, f);
    fclose(f);
    printf("Book added.\n");
}
static void updateBook(void) {
    printf("Enter Book ID to update: ");
    int id;
    if (!readInt(&id)) { printf("Invalid input.\n"); return; }
    FILE *f = fopen(DATA_FILE, "rb+");
    if (!f) { printf("No books.\n"); return; }
    struct Book b; int found = 0;
    while (fread(&b, sizeof(b), 1, f)) {
        if (b.id == id) {
            getchar();
            printf("Enter new Title: "); readLineSafe(b.title, sizeof(b.title));
            printf("Enter new Author: "); readLineSafe(b.author, sizeof(b.author));
            fseek(f, - (long)sizeof(b), SEEK_CUR);
            fwrite(&b, sizeof(b), 1, f);
            printf("Book updated.\n");
            found = 1; break;
        }
    }
    fclose(f);
    if (!found) printf("Book not found.\n");
}
static void deleteBook(void) {
    printf("Enter Book ID to delete: ");
    int id; if (!readInt(&id)) { printf("Invalid input.\n"); return; }
    if (bookIsIssued(id)) { printf("Book currently issued - cannot delete.\n"); return; }
    FILE *src = fopen(DATA_FILE, "rb");
    if (!src) { printf("No books.\n"); return; }
    FILE *dst = fopen("tmp_books.dat", "wb");
    if (!dst) { fclose(src); printf("Unable to open temp file.\n"); return; }
    struct Book b; int found = 0;
    while (fread(&b, sizeof(b), 1, src)) {
        if (b.id == id) { found = 1; continue; }
        fwrite(&b, sizeof(b), 1, dst);
    }
    fclose(src); fclose(dst);
    remove(DATA_FILE); rename("tmp_books.dat", DATA_FILE);
    if (found) printf("Book deleted.\n"); else printf("Book not found.\n");
}
static void viewAllBooksSorted(void) {
    struct Book *arr = NULL;
    size_t cap = 0, count = 0;
    FILE *f = fopen(DATA_FILE, "rb");
    if (f) {
        struct Book b;
        while (fread(&b, sizeof(b), 1, f)) {
            if (count + 1 > cap) {
                cap = cap ? cap * 2 : 64;
                arr = realloc(arr, cap * sizeof(*arr));
                if (!arr) { fclose(f); printf("Memory error.\n"); return; }
            }
            arr[count++] = b;
        }
        fclose(f);
    }
    if (count == 0) { printf("No books.\n"); free(arr); return; }
    printf("Sort by 1-ID 2-Title (enter choice): ");
    int c; if (!readInt(&c)) { printf("Invalid choice.\n"); free(arr); return; }
    for (size_t i = 0; i + 1 < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            int swap = 0;
            if (c == 1) swap = (arr[i].id > arr[j].id);
            else swap = (strcmp(arr[i].title, arr[j].title) > 0);
            if (swap) { struct Book t = arr[i]; arr[i] = arr[j]; arr[j] = t; }
        }
    }
    printf("\n%-5s %-30s %-20s %-10s\n", "ID", "Title", "Author", "Status");
    printf("----------------------------------------------------------------\n");
    for (size_t i = 0; i < count; i++) {
        printf("%-5d %-30s %-20s %-10s\n", arr[i].id, arr[i].title, arr[i].author,
               arr[i].available ? "Available" : "Issued");
    }
    free(arr);
}
static void viewAvailableBooks(void) {
    FILE *f = fopen(DATA_FILE, "rb");
    if (!f) { printf("No books.\n"); return; }
    printf("\n%-5s %-30s %-20s\n", "ID", "Title", "Author");
    printf("-------------------------------------------------\n");
    struct Book b;
    while (fread(&b, sizeof(b), 1, f)) {
        if (b.available) printf("%-5d %-30s %-20s\n", b.id, b.title, b.author);
    }
    fclose(f);
}

static void toLowerStr(char *s) {
    for (int i = 0; s[i]; i++) s[i] = (char)tolower((unsigned char)s[i]);
}
static int matchWords(const char *text, const char *keyword) {
    char textLower[300], keyLower[300];
    strncpy(textLower, text, sizeof(textLower)-1); textLower[sizeof(textLower)-1]=0;
    strncpy(keyLower, keyword, sizeof(keyLower)-1); keyLower[sizeof(keyLower)-1]=0;
    toLowerStr(textLower); toLowerStr(keyLower);
    char *tok = strtok(keyLower, " ");
    while (tok) {
        if (!strstr(textLower, tok)) return 0;
        tok = strtok(NULL, " ");
    }
    return 1;
}

static void searchByKeyword(void) {
    printf("Enter keyword (title or author): ");
    char keyword[200]; readLineSafe(keyword, sizeof(keyword));
    if (keyword[0]==0) { printf("Empty keyword.\n"); return; }
    FILE *f = fopen(DATA_FILE, "rb");
    if (!f) { printf("No books.\n"); return; }
    int found = 0;
    printf("\n%-5s %-30s %-20s %-10s\n", "ID", "Title", "Author", "Status");
    printf("----------------------------------------------------------------\n");
    struct Book b;
    while (fread(&b, sizeof(b), 1, f)) {
        char t[200]; strncpy(t, b.title, sizeof(t)); t[sizeof(t)-1]=0;
        char a[200]; strncpy(a, b.author, sizeof(a)); a[sizeof(a)-1]=0;
        if (matchWords(t, keyword) || matchWords(a, keyword)) {
            printf("%-5d %-30s %-20s %-10s\n", b.id, b.title, b.author, b.available ? "Available" : "Issued");
            found = 1;
        }
    }
    fclose(f);
    if (!found) printf("No matching books.\n");
}
static void addStudent(void) {
    printf("Enter Student ID: ");
    int id; if (!readInt(&id)) { printf("Invalid ID.\n"); return; }
    if (id <= 0) { printf("ID must be positive.\n"); return; }
    if (studentIdDuplicate(id)) { printf("Student ID already exists.\n"); return; }
    struct Student s; s.id = id;
    getchar(); 
    printf("Enter Student Name: "); readLineSafe(s.name, sizeof(s.name));
    FILE *f = fopen(STUDENT_FILE, "ab");
    if (!f) { printf("Unable to open student file.\n"); return; }
    fwrite(&s, sizeof(s), 1, f); fclose(f);
    printf("Student added.\n");
}

static void removeStudent(void) {
    printf("Enter Student ID to remove: ");
    int id; if (!readInt(&id)) { printf("Invalid input.\n"); return; }
    if (studentHasUnreturned(id)) { printf("Student has unreturned books. Cannot remove.\n"); return; }
    FILE *src = fopen(STUDENT_FILE, "rb");
    if (!src) { printf("No students.\n"); return; }
    FILE *dst = fopen("tmp_students.dat", "wb");
    if (!dst) { fclose(src); printf("Unable to open temp file.\n"); return; }
    struct Student s; int found = 0;
    while (fread(&s, sizeof(s), 1, src)) {
        if (s.id == id) { found = 1; continue; }
        fwrite(&s, sizeof(s), 1, dst);
    }
    fclose(src); fclose(dst);
    remove(STUDENT_FILE); rename("tmp_students.dat", STUDENT_FILE);
    if (found) printf("Student removed.\n"); else printf("Student not found.\n");
}
static struct Issue *loadAllIssues(size_t *outCount) {
    *outCount = 0;
    FILE *fi = fopen(ISSUE_FILE, "rb");
    if (!fi) return NULL;
    struct Issue *arr = NULL;
    size_t cap = 0, count = 0;
    struct Issue tmp;
    while (fread(&tmp, sizeof(tmp), 1, fi)) {
        if (count + 1 > cap) {
            cap = cap ? cap * 2 : 128;
            arr = realloc(arr, cap * sizeof(*arr));
            if (!arr) { fclose(fi); return NULL; }
        }
        arr[count++] = tmp;
    }
    fclose(fi);
    *outCount = count;
    return arr;
}

static int getStudentNameById(int sid, char *buf, size_t sz) {
    return studentExists(sid, buf);
}

static void issueBook(int requester_student_id) {
    char sname[120];
    if (!getStudentNameById(requester_student_id, sname, sizeof(sname))) {
        printf("Student not found. Contact admin.\n"); return;
    }
    printf("Enter Book ID to issue: ");
    int book_id; if (!readInt(&book_id)) { printf("Invalid input.\n"); return; }
    struct Book b;
    if (!bookExists(book_id, &b)) { printf("Book not found.\n"); return; }
    if (!b.available) { printf("Book not available.\n"); return; }
    FILE *fb = fopen(DATA_FILE, "rb+");
    if (fb) {
        struct Book tb;
        while (fread(&tb, sizeof(tb), 1, fb)) {
            if (tb.id == book_id) {
                tb.available = 0;
                fseek(fb, - (long)sizeof(tb), SEEK_CUR);
                fwrite(&tb, sizeof(tb), 1, fb);
                break;
            }
        }
        fclose(fb);
    }
    struct Issue iss;
    iss.book_id = book_id;
    iss.student_id = requester_student_id;
    iss.issue_time = time(NULL);
    printf("Enter due days (e.g., 14): ");
    int dd; if (!readInt(&dd)) dd = 14;
    if (dd <= 0) dd = 14;
    iss.due_days = dd;
    iss.returned = 0;
    iss.return_time = 0;
    FILE *fi = fopen(ISSUE_FILE, "ab");
    if (!fi) { printf("Unable to write issue record.\n"); return; }
    fwrite(&iss, sizeof(iss), 1, fi);
    fclose(fi);
    printf("Book issued to %s (ID %d). Due in %d days.\n", sname, requester_student_id, iss.due_days);
}

static void returnBookByStudent(int requester_student_id) {
    printf("Enter Book ID to return: ");
    int book_id; if (!readInt(&book_id)) { printf("Invalid input.\n"); return; }
    size_t count = 0; struct Issue *all = loadAllIssues(&count);
    if (!all) { printf("No issue records.\n"); return; }
    int foundIdx = -1;
    for (size_t i = 0; i < count; i++) {
        if (all[i].book_id == book_id && all[i].student_id == requester_student_id && all[i].returned == 0) {
            foundIdx = (int)i; break;
        }
    }
    if (foundIdx < 0) { printf("No matching issue record found for this student.\n"); free(all); return; }
    all[foundIdx].returned = 1;
    all[foundIdx].return_time = time(NULL);
    FILE *fi = fopen(ISSUE_FILE, "wb");
    if (!fi) { printf("Unable to update issue records.\n"); free(all); return; }
    for (size_t i = 0; i < count; i++) fwrite(&all[i], sizeof(all[i]), 1, fi);
    fclose(fi);
    FILE *fb = fopen(DATA_FILE, "rb+");
    if (fb) {
        struct Book b;
        while (fread(&b, sizeof(b), 1, fb)) {
            if (b.id == book_id) {
                b.available = 1;
                fseek(fb, - (long)sizeof(b), SEEK_CUR);
                fwrite(&b, sizeof(b), 1, fb);
                break;
            }
        }
        fclose(fb);
    }
    time_t issueT = all[foundIdx].issue_time;
    int due_days = all[foundIdx].due_days;
    time_t due = issueT + (time_t)due_days * 24 * 3600;
    time_t now = all[foundIdx].return_time;
    double secondsLate = difftime(now, due);
    long daysLate = secondsLate > 0 ? (long)((secondsLate + 24*3600 - 1) / (24*3600)) : 0; // ceil-ish
    long fine = daysLate * FINE_PER_DAY;
    char sname[120]; getStudentNameById(requester_student_id, sname, sizeof(sname));
    printf("Book returned by %s (ID %d).\n", sname, requester_student_id);
    if (daysLate > 0) printf("Late by %ld day(s). Fine: ₹%ld\n", daysLate, fine);
    else printf("Returned on time. No fine.\n");
    free(all);
}
static void viewStudentIssued(int student_id) {
    FILE *fi = fopen(ISSUE_FILE, "rb");
    if (!fi) { printf("No issue records.\n"); return; }
    int found = 0;
    struct Issue iss;
    while (fread(&iss, sizeof(iss), 1, fi)) {
        if (iss.student_id == student_id && !iss.returned) {
            char it[64], dt[64]; struct tm tm1;
            safeLocalTime(&tm1, &iss.issue_time);
            strftime(it, sizeof(it), "%Y-%m-%d", &tm1);
            time_t due = iss.issue_time + (time_t)iss.due_days * 24 * 3600;
            safeLocalTime(&tm1, &due);
            strftime(dt, sizeof(dt), "%Y-%m-%d", &tm1);
            printf("Book ID: %d | Issued: %s | Due: %s\n", iss.book_id, it, dt);
            found = 1;
        }
    }
    fclose(fi);
    if (!found) printf("No issued books for this student.\n");
}
static void viewIssuedReport(void) {
    FILE *fi = fopen(ISSUE_FILE, "rb");
    if (!fi) { printf("No issue records.\n"); return; }
    printf("\nBookID StudentID IssueDate  DueDate    Returned ReturnDate\n");
    struct Issue iss;
    while (fread(&iss, sizeof(iss), 1, fi)) {
        char idt[64], ddt[64], rdt[64]; struct tm tm1;
        safeLocalTime(&tm1, &iss.issue_time);
        strftime(idt, sizeof(idt), "%Y-%m-%d", &tm1);
        time_t due = iss.issue_time + (time_t)iss.due_days * 24 * 3600;
        safeLocalTime(&tm1, &due);
        strftime(ddt, sizeof(ddt), "%Y-%m-%d", &tm1);
        if (iss.returned) { safeLocalTime(&tm1, &iss.return_time); strftime(rdt, sizeof(rdt), "%Y-%m-%d", &tm1); }
        else strcpy(rdt, "-");
        printf("%-6d %-9d %-10s %-10s %-8s %s\n",
               iss.book_id, iss.student_id, idt, ddt, iss.returned ? "Yes" : "No", rdt);
    }
    fclose(fi);
    printf("\nExport issued report to CSV? (y/n): ");
    char ans[8]; readLineSafe(ans, sizeof(ans));
    if (ans[0]=='y' || ans[0]=='Y') {
        FILE *fout = fopen("issued_report.csv", "w");
        if (!fout) { printf("Unable to write CSV.\n"); return; }
        fprintf(fout, "BookID,StudentID,IssueDate,DueDate,Returned,ReturnDate\n");
        fi = fopen(ISSUE_FILE, "rb");
        while (fread(&iss, sizeof(iss), 1, fi)) {
            char idt[64], ddt[64], rdt[64]; struct tm tm1;
            safeLocalTime(&tm1, &iss.issue_time);
            strftime(idt, sizeof(idt), "%Y-%m-%d", &tm1);
            time_t due = iss.issue_time + (time_t)iss.due_days * 24 * 3600;
            safeLocalTime(&tm1, &due);
            strftime(ddt, sizeof(ddt), "%Y-%m-%d", &tm1);
            if (iss.returned) { safeLocalTime(&tm1, &iss.return_time); strftime(rdt, sizeof(rdt), "%Y-%m-%d", &tm1); }
            else strcpy(rdt, "-");
            fprintf(fout, "%d,%d,%s,%s,%s,%s\n", iss.book_id, iss.student_id, idt, ddt, iss.returned ? "Yes" : "No", rdt);
        }
        fclose(fi); fclose(fout);
        printf("Exported to issued_report.csv\n");
    }
}
static void checkOverdue(void) {
    FILE *fi = fopen(ISSUE_FILE, "rb");
    if (!fi) { printf("No issue records.\n"); return; }
    int any = 0; time_t now = time(NULL);
    struct Issue iss;
    while (fread(&iss, sizeof(iss), 1, fi)) {
        if (!iss.returned) {
            time_t due = iss.issue_time + (time_t)iss.due_days * 24 * 3600;
            if (now > due) {
                char idt[64], ddt[64]; struct tm tm1;
                safeLocalTime(&tm1, &iss.issue_time);
                strftime(idt, sizeof(idt), "%Y-%m-%d", &tm1);
                safeLocalTime(&tm1, &due);
                strftime(ddt, sizeof(ddt), "%Y-%m-%d", &tm1);
                printf("Overdue -> BookID %d | StudentID %d | Issued: %s | Due: %s\n",
                       iss.book_id, iss.student_id, idt, ddt);
                any = 1;
            }
        }
    }
    fclose(fi);
    if (!any) printf("No overdue books.\n");
    else {
        printf("\nExport overdue report to CSV? (y/n): ");
        char ans[8]; readLineSafe(ans, sizeof(ans));
        if (ans[0]=='y' || ans[0]=='Y') {
            FILE *fout = fopen("overdue_report.csv", "w");
            if (!fout) { printf("Unable to write CSV.\n"); return; }
            fprintf(fout, "BookID,StudentID,IssueDate,DueDate,DaysOverdue,Fine\n");
            fi = fopen(ISSUE_FILE, "rb");
            time_t now2 = time(NULL);
            while (fread(&iss, sizeof(iss), 1, fi)) {
                if (!iss.returned) {
                    time_t due = iss.issue_time + (time_t)iss.due_days * 24 * 3600;
                    if (now2 > due) {
                        char idt[64], ddt[64]; struct tm tm1;
                        safeLocalTime(&tm1, &iss.issue_time);
                        strftime(idt, sizeof(idt), "%Y-%m-%d", &tm1);
                        safeLocalTime(&tm1, &due);
                        strftime(ddt, sizeof(ddt), "%Y-%m-%d", &tm1);
                        double secondsLate = difftime(now2, due);
                        long daysLate = secondsLate > 0 ? (long)((secondsLate + 24*3600 - 1) / (24*3600)) : 0;
                        long fine = daysLate * FINE_PER_DAY;
                        fprintf(fout, "%d,%d,%s,%s,%ld,%ld\n", iss.book_id, iss.student_id, idt, ddt, daysLate, fine);
                    }
                }
            }
            fclose(fi); fclose(fout);
            printf("Exported to overdue_report.csv\n");
        }
    }
}
static void searchStudentByName(void) {
    printf("Enter name keyword: ");
    char key[200]; readLineSafe(key, sizeof(key)); if (key[0]==0) { printf("Empty.\n"); return; }
    FILE *f = fopen(STUDENT_FILE, "rb");
    if (!f) { printf("No students.\n"); return; }
    struct Student s; int found = 0;
    while (fread(&s, sizeof(s), 1, f)) {
        if (matchWords(s.name, key)) {
            printf("ID: %d | Name: %s\n", s.id, s.name);
            found = 1;
        }
    }
    fclose(f);
    if (!found) printf("No matching students.\n");
}
static void studentHistory(int student_id) {
    FILE *fi = fopen(ISSUE_FILE, "rb");
    if (!fi) { printf("No issue records.\n"); return; }
    int found = 0; struct Issue iss;
    printf("History for student ID %d:\n", student_id);
    while (fread(&iss, sizeof(iss), 1, fi)) {
        if (iss.student_id == student_id) {
            char it[64], rt[64]; struct tm tm1;
            safeLocalTime(&tm1, &iss.issue_time);
            strftime(it, sizeof(it), "%Y-%m-%d", &tm1);
            if (iss.returned) { safeLocalTime(&tm1, &iss.return_time); strftime(rt, sizeof(rt), "%Y-%m-%d", &tm1); }
            else strcpy(rt, "-");
            printf("Book %d | Issued %s | Due %d days | Returned %s\n", iss.book_id, it, iss.due_days, rt);
            found = 1;
        }
    }
    fclose(fi);
    if (!found) printf("No history for this student.\n");
}
static int copyFile(const char *src, const char *dst) {
    FILE *s = fopen(src, "rb");
    if (!s) return 0;
    FILE *d = fopen(dst, "wb");
    if (!d) { fclose(s); return 0; }
    char buf[4096]; size_t r;
    while ((r = fread(buf, 1, sizeof(buf), s)) > 0) {
        if (fwrite(buf, 1, r, d) != r) { fclose(s); fclose(d); return 0; }
    }
    fclose(s); fclose(d); return 1;
}

static void backupDatabase(void) {
    int ok1 = copyFile(DATA_FILE, "books_backup.dat");
    int ok2 = copyFile(STUDENT_FILE, "students_backup.dat");
    int ok3 = copyFile(ISSUE_FILE, "issues_backup.dat");
    if (ok1 || ok2 || ok3) printf("Backup completed.\n"); else printf("Nothing to backup or failed.\n");
}
static void restoreDatabase(void) {
    int ok1 = copyFile("books_backup.dat", DATA_FILE);
    int ok2 = copyFile("students_backup.dat", STUDENT_FILE);
    int ok3 = copyFile("issues_backup.dat", ISSUE_FILE);
    if (ok1 || ok2 || ok3) printf("Restore completed.\n"); else printf("No backup files found.\n");
}
static void adminMenu(void) {
    while (1) {
        printf("\n--- Admin Menu ---\n");
        printf("1. Add Book\n2. Update Book\n3. Delete Book\n4. View All Books (sorted)\n5. View Issued Report\n6. Check Overdue Books\n7. Add Student\n8. Remove Student\n9. Backup (all)\n10. Restore (all)\n11. Change Admin Password\n12. Reset Admin Password to Default\n13. Search Student by Name\n14. View Student History\n15. Export Overdue Report CSV\n16. Back\nEnter choice: ");
        int ch; if (!readInt(&ch)) { printf("Invalid.\n"); continue; }
        switch (ch) {
            case 1: addBook(); break;
            case 2: updateBook(); break;
            case 3: deleteBook(); break;
            case 4: viewAllBooksSorted(); break;
            case 5: viewIssuedReport(); break;
            case 6: checkOverdue(); break;
            case 7: addStudent(); break;
            case 8: removeStudent(); break;
            case 9: backupDatabase(); break;
            case 10: restoreDatabase(); break;
            case 11: changeAdminPassword(); break;
            case 12: resetAdminPasswordToDefault(); break;
            case 13: searchStudentByName(); break;
            case 14: {
                printf("Enter Student ID: ");
                int sid; if (!readInt(&sid)) { printf("Invalid.\n"); break; }
                studentHistory(sid);
                break;
            }
            case 15: {
                checkOverdue();
                break;
            }
            case 16: return;
            default: printf("Invalid.\n");
        }
        pauseForUser();
    }
}

static void studentMenu(int student_id) {
    char sname[120]; studentExists(student_id, sname);
    while (1) {
        printf("\n--- Student Menu (ID %d) ---\n", student_id);
        printf("1. View All Books\n2. View Available Books\n3. Search Book (keyword)\n4. Issue Book\n5. Return Book\n6. View My Issued Books\n7. My History\n8. Back\nEnter choice: ");
        int ch; if (!readInt(&ch)) { printf("Invalid.\n"); continue; }
        switch (ch) {
            case 1: viewAllBooksSorted(); break;
            case 2: viewAvailableBooks(); break;
            case 3: searchByKeyword(); break;
            case 4: issueBook(student_id); break;
            case 5: returnBookByStudent(student_id); break;
            case 6: viewStudentIssued(student_id); break;
            case 7: studentHistory(student_id); break;
            case 8: return;
            default: printf("Invalid.\n");
        }
        pauseForUser();
    }
}

int main(void) {
    ensureDataFilesExist();
    while (1) {
        printf("\n--- Library System ---\n1. Student Mode\n2. Admin Mode\n3. Exit\nEnter choice: ");
        int mode; if (!readInt(&mode)) { printf("Invalid choice.\n"); continue; }
        if (mode == 1) {
            printf("Enter Student ID: ");
            int sid; if (!readInt(&sid)) { printf("Invalid ID.\n"); continue; }
            char name[120];
            if (!studentExists(sid, name)) { printf("Student not found. Please contact admin.\n"); continue; }
            printf("Welcome %s (ID %d)\n", name, sid);
            studentMenu(sid);
        } else if (mode == 2) {
            if (adminLogin()) adminMenu();
            else printf("Access denied.\n");
        } else if (mode == 3) {
            printf("Exiting.\n"); break;
        } else printf("Invalid choice.\n");
    }
    return 0;
}
/*features
Password is "admin123" by default
*/