#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <stdint.h>
#include <stddef.h>


#define SUCCESS 0
#define INVALID_CURSOR_POS -1
#define OUTDATED_VERSION -3

// 5 Structure: Define formatting types
enum Formatting {
    NONE,
    CODE,
    HEADING_1,
    HEADING_2,
    HEADING_3,
    BLOCKQUOTE,
    ORDERED_LIST,
    UNORDERED_LIST,
    HORIZONTAL_RULE,
    LINK
};


// 5 Structure: Define operation types
enum OperationType {
    OP_INSERT,
    OP_DELETE,
    OP_BOLD,   
    OP_ITALIC  
};
// 5 Structure: Define operation structure
typedef struct operation {
    enum OperationType type;
    uint64_t version; // Version when this operation was performed
    size_t pos; // Position of the operation
    size_t len; // Length (for delete) or string length (for insert)
    char* content; // Content (for insert, NULL for delete)
    struct operation* next;
} operation;

// 5 Structure: Define chunk structure
typedef struct chunk {
    char data;
    int is_bold;
    int is_italic;
    uint64_t version; // When this chunk was inserted
    uint64_t format_version; // <------ Added to track when formatting was applied
    enum Formatting formatting;
    struct chunk* next;
} chunk;
// 5 Structure: Define document structure
typedef struct document {
    chunk* head;
    size_t length;
    uint64_t version;
    operation* ops;
} document;


#endif