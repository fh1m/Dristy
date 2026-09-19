#ifndef FILE_WRITE_ERROR_H
#define FILE_WRITE_ERROR_H


void file_write_set_error(const char *error);
void file_write_clear_error(void);
const char *file_write_last_error(void);

#endif
