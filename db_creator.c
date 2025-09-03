#include <sqlite3.h>
#include <stdio.h>

int main(void) {
    sqlite3 *db;
    char *err_msg = 0;

    // Attempt to open/create the database file
    int rc = sqlite3_open("questions.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    // SQL statement to create the 'maths' table
    const char *sql_create_table = "CREATE TABLE IF NOT EXISTS maths("
                                   "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                   "  question TEXT NOT NULL,"
                                   "  answer1 TEXT NOT NULL,"
                                   "  answer2 TEXT NOT NULL,"
                                   "  answer3 TEXT NOT NULL,"
                                   "  answer4 TEXT NOT NULL,"
                                   "  correctAnswer INTEGER NOT NULL"
                                   ");";

    rc = sqlite3_exec(db, sql_create_table, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }

    printf("Table 'maths' created successfully.\n");

    // SQL statements to insert sample questions
    const char *sql_insert[] = {
        "INSERT INTO maths (question, answer1, answer2, answer3, answer4, "
        "correctAnswer) VALUES ('What is 5 x 8?', '35', '40', '45', '50', 2);",
        "INSERT INTO maths (question, answer1, answer2, answer3, answer4, "
        "correctAnswer) VALUES ('What is 12 + 19?', '29', '30', '31', '32', "
        "3);",
        "INSERT INTO maths (question, answer1, answer2, answer3, answer4, "
        "correctAnswer) VALUES ('What is 50 / 2?', '20', '25', '30', '35', 2);",
        "INSERT INTO maths (question, answer1, answer2, answer3, answer4, "
        "correctAnswer) VALUES ('What is 100 - 42?', '58', '62', '56', '68', "
        "1);",
        "INSERT INTO maths (question, answer1, answer2, answer3, answer4, "
        "correctAnswer) VALUES ('What is the square root of 81?', '7', '8', "
        "'9', '10', 3);",
        NULL // Sentinel to mark the end of the array
    };

    for (int i = 0; sql_insert[i] != NULL; i++) {
        rc = sqlite3_exec(db, sql_insert[i], 0, 0, &err_msg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error inserting data: %s\n", err_msg);
            sqlite3_free(err_msg);
        }
    }

    printf("Sample questions inserted successfully.\n");

    sqlite3_close(db);

    return 0;
}
