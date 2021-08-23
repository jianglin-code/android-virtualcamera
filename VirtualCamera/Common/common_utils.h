#ifndef common_utils_h
#define common_utils_h

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

static int load_file_content(const char *filename, void **content) {
    FILE *fp = fopen(filename, "rb");
    int file_len = 0, read_len = 0;
    void *memory = NULL;
    
    *content = NULL;
    if (fp) {
        fseek(fp, 0, SEEK_END);
        file_len = (int)ftell(fp);
        fseek(fp, 0, SEEK_SET);
    }
    
    if (file_len > 0) {
        memory = malloc(file_len);
        read_len = (int)fread(memory, 1, file_len, fp);
        
        if (read_len == file_len) {
            *content = memory;
        } else {
            read_len = 0;
            free(memory);
        }
        fclose(fp);
    }
    
    return read_len;
}

#endif /* common_utils_h */
