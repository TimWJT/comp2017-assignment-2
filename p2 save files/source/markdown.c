#include "../libs/markdown.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define SUCCESS 0
#define INVALID_CURSOR_POS -1
#define OUTDATED_VERSION -3

// === Init and Free ===
// 5 Structure: Initialize a new document
document *markdown_init(void) {
    // Allocate document
    document* doc = malloc(sizeof(document));
    // Initialize empty document
    doc->head = NULL;
    doc->length = 0;
    doc->version = 0;
    doc->ops = NULL;
    return doc;
}

void markdown_free(document *doc) {
    // Check for NULL document
    if (doc == NULL) {
        return;
    }
    // Free all chunks in the linked list
    chunk* current = doc->head;
    while (current != NULL) {
        chunk* temp = current;
        current = current->next;
        free(temp);
    }
    // Free all operations
    operation* op = doc->ops;
    while (op != NULL) {
        operation* temp = op;
        op = op->next;
        if (temp->content != NULL) {
            free(temp->content);
        }
        free(temp);
    }
    // Free the document itself
    free(doc);
}

// 7.1 Helper: Compute committed length up to version - 1
size_t compute_committed_length(const document* doc) {
    size_t length = 0;
    // Collect operations in reverse order (newest first)
    operation* ops_list = NULL;
    operation* op = doc->ops;
    while (op != NULL) {
        operation* new_op = malloc(sizeof(operation));
        *new_op = *op;
        new_op->next = ops_list;
        if (op->content != NULL) {
            new_op->content = malloc(strlen(op->content) + 1);
            strcpy(new_op->content, op->content);
        }
        ops_list = new_op;
        op = op->next;
    }
    // Reverse the list to process operations in chronological order (oldest first)
    operation* ordered_ops = NULL;
    while (ops_list != NULL) {
        operation* temp = ops_list;
        ops_list = ops_list->next;
        temp->next = ordered_ops;
        ordered_ops = temp;
    }
    // Apply operations up to version - 1
    op = ordered_ops;
    while (op != NULL) {
        if (op->version >= doc->version) {
            op = op->next;
            continue;
        }
        if (op->type == OP_INSERT) {
            length += op->len;
        } else if (op->type == OP_DELETE) {
            size_t len = op->len;
            if (op->pos + len > length) {
                len = length - op->pos;
            }
            length -= len;
        }
        op = op->next;
    }
    // Free operations list
    op = ordered_ops;
    while (op != NULL) {
        operation* temp = op;
        op = op->next;
        if (temp->content != NULL) {
            free(temp->content);
        }
        free(temp);
    }
    return length;
}

// === Edit Commands ===
// 7.1 INSERT command: Insert content at position
int markdown_insert(document* doc, uint64_t version, size_t pos, const char* content) {
    // 7.4 Check version
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    // 7.4 Check position against committed length
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) {
        return INVALID_CURSOR_POS;
    }
    // Create new operation
    operation* new_op = malloc(sizeof(operation));
    new_op->type = OP_INSERT;
    new_op->version = version;
    new_op->pos = pos;
    new_op->len = strlen(content);
    new_op->content = malloc(new_op->len + 1);
    strcpy(new_op->content, content);
    new_op->next = doc->ops;
    doc->ops = new_op;
    doc->length += new_op->len; // Update pending length
    return SUCCESS;
}

// 7.1 DEL command: Delete characters at position
int markdown_delete(document* doc, uint64_t version, size_t pos, size_t len) {
    // 7.4 Check version
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    // 7.4 Check position against committed length
    size_t committed_length = compute_committed_length(doc);
    if (pos >= committed_length) {
        return INVALID_CURSOR_POS;
    }
    // Create new operation
    operation* new_op = malloc(sizeof(operation));
    new_op->type = OP_DELETE;
    new_op->version = version;
    new_op->pos = pos;
    new_op->len = len;
    new_op->content = NULL;
    new_op->next = doc->ops;
    doc->ops = new_op;
    if (pos + len > doc->length) {
        len = doc->length - pos;
    }
    doc->length -= len; // Update pending length
    return SUCCESS;
}

// === Formatting Commands ===
// NEWLINE <pos>
int markdown_newline(document *doc, uint64_t version, size_t pos) {
    if (doc == NULL) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    // 7.4 Check position against committed length
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) {
        return INVALID_CURSOR_POS;
    }
    char nl[2] = {'\n','\0'};
    return markdown_insert(doc, version, pos, nl);
}

int markdown_heading(document *doc, uint64_t version, size_t level, size_t pos) {
    if (doc == NULL) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    if (level < 1 || level > 3) return INVALID_CURSOR_POS;
    // 7.4 Check position against committed length
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) {
        return INVALID_CURSOR_POS;
    }
    char prefix[5] = {0};
    for (size_t i = 0; i < level; i++) prefix[i] = '#';
    prefix[level] = ' ';
    return markdown_insert(doc, version, pos, prefix);
}

// BLOCKQUOTE <pos>
int markdown_blockquote(document *doc, uint64_t version, size_t pos) {
    if (doc == NULL) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    // 7.4 Check position against committed length
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) {
        return INVALID_CURSOR_POS;
    }
    return markdown_insert(doc, version, pos, "> ");
}

// ORDERED_LIST <pos>
int markdown_ordered_list(document *doc, uint64_t version, size_t pos) {
    if (doc == NULL) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    // 7.4 Check position against committed length
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) {
        return INVALID_CURSOR_POS;
    }
    // insert "1. "
    return markdown_insert(doc, version, pos, "1. ");
}

// UNORDERED_LIST <pos>
int markdown_unordered_list(document *doc, uint64_t version, size_t pos) {
    if (doc == NULL) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    // 7.4 Check position against committed length
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) {
        return INVALID_CURSOR_POS;
    }
    // insert "- "
    return markdown_insert(doc, version, pos, "- ");
}

// CODE <start> <end>
int markdown_code(document *doc, uint64_t version, size_t start, size_t end) {
    if (doc == NULL) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    // 7.4 Check range against committed length
    size_t committed_length = compute_committed_length(doc);
    if (start >= committed_length || end > committed_length || start >= end) {
        return INVALID_CURSOR_POS;
    }
    // wrap with backticks: insert trailing then leading
    markdown_insert(doc, version, end, "`");
    return markdown_insert(doc, version, start, "`");
}

// HORIZONTAL_RULE <pos>
int markdown_horizontal_rule(document *doc, uint64_t version, size_t pos) {
    if (doc == NULL) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    // 7.4 Check position against committed length
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) {
        return INVALID_CURSOR_POS;
    }
    // insert "---\n"
    return markdown_insert(doc, version, pos, "---\n");
}

// LINK <start> <end> <url>
int markdown_link(document *doc, uint64_t version, size_t start, size_t end, const char *url) {
    if (doc == NULL) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    // 7.4 Check range against committed length
    size_t committed_length = compute_committed_length(doc);
    if (start >= committed_length || end > committed_length || start >= end) {
        return INVALID_CURSOR_POS;
    }
    // wrap selection: insert in reverse order to keep indices valid
    markdown_insert(doc, version, end, ")");
    markdown_insert(doc, version, end, url);
    markdown_insert(doc, version, end, "(");
    markdown_insert(doc, version, end, "]");
    return markdown_insert(doc, version, start, "[");
}

// 7.1 BOLD command: Apply bold formatting
int markdown_bold(document* doc, uint64_t version, size_t start, size_t end) {
    // 7.4 Check version
    if (version != doc->version) {
        return OUTDATED_VERSION;
    }
    // 7.4 Check positions against committed length
    size_t committed_length = compute_committed_length(doc);
    if (start >= committed_length || end > committed_length || start >= end) {
        return INVALID_CURSOR_POS;
    }
    // Create new operation
    operation* new_op = malloc(sizeof(operation));
    new_op->type = OP_BOLD;
    new_op->version = version;
    new_op->pos = start;
    new_op->len = end - start;
    new_op->content = NULL;
    new_op->next = doc->ops;
    doc->ops = new_op;
    return SUCCESS;
}

int markdown_italic(document *doc, uint64_t version, size_t start, size_t end) {
    // Check for NULL document
    if (doc == NULL) {
        return INVALID_CURSOR_POS;
    }
    // Check version
    if (doc->version != version) {
        return OUTDATED_VERSION;
    }
    // Check if range is valid against committed length
    size_t committed_length = compute_committed_length(doc);
    if (start >= committed_length || end > committed_length || start >= end) {
        return INVALID_CURSOR_POS;
    }
    // Create new operation
    operation* new_op = malloc(sizeof(operation));
    new_op->type = OP_ITALIC;
    new_op->version = version;
    new_op->pos = start;
    new_op->len = end - start;
    new_op->content = NULL;
    new_op->next = doc->ops;
    doc->ops = new_op;
    return SUCCESS;
}

// === Utilities ===
// 7.1 Utility: Print document
void markdown_print(const document *doc, FILE *stream) {
    // Check for NULL document or stream
    if (doc == NULL || stream == NULL) {
        return;
    }
    // First, collect all operations in a list (reverse order, newest first)
    operation* ops_list = NULL;
    operation* op = doc->ops;
    while (op != NULL) {
        operation* new_op = malloc(sizeof(operation));
        *new_op = *op;
        new_op->next = ops_list;
        if (op->content != NULL) {
            new_op->content = malloc(strlen(op->content) + 1);
            strcpy(new_op->content, op->content);
        }
        ops_list = new_op;
        op = op->next;
    }
    // Build document state up to version - 1
    chunk* head = NULL;
    size_t length = 0;
    // Apply operations in order (newest first for correct insertion order at same position)
    op = ops_list;
    while (op != NULL) {
        if (op->version >= doc->version) {
            op = op->next;
            continue;
        }
        if (op->type == OP_INSERT) {
            chunk* new_chunk = NULL;
            chunk* prev = NULL;
            chunk* current = head;
            size_t i = 0;
            while (current != NULL && i < op->pos) {
                prev = current;
                current = current->next;
                i++;
            }
            for (size_t j = 0; j < op->len; j++) {
                new_chunk = malloc(sizeof(chunk));
                new_chunk->data = op->content[j];
                new_chunk->is_bold = 0;
                new_chunk->is_italic = 0;
                new_chunk->version = op->version;
                new_chunk->format_version = op->version;
                new_chunk->formatting = NONE;
                new_chunk->next = current;
                if (prev == NULL) {
                    head = new_chunk;
                } else {
                    prev->next = new_chunk;
                }
                prev = new_chunk;
                current = new_chunk->next;
                length++;
            }
        } else if (op->type == OP_DELETE) {
            chunk* prev = NULL;
            chunk* current = head;
            size_t i = 0;
            while (current != NULL && i < op->pos) {
                prev = current;
                current = current->next;
                i++;
            }
            for (size_t j = 0; j < op->len && current != NULL; j++) {
                chunk* to_delete = current;
                current = current->next;
                if (prev == NULL) {
                    head = current;
                } else {
                    prev->next = current;
                }
                free(to_delete);
                length--;
            }
        } else if (op->type == OP_BOLD || op->type == OP_ITALIC) {
            chunk* current = head;
            size_t i = 0;
            while (current != NULL && i < length) {
                if (i >= op->pos && i < op->pos + op->len) {
                    if (op->type == OP_BOLD) {
                        current->is_bold = 1;
                        current->format_version = op->version;
                    } else if (op->type == OP_ITALIC) {
                        current->is_italic = 1;
                        current->format_version = op->version;
                    }
                }
                current = current->next;
                i++;
            }
        }
        op = op->next;
    }
    // Print the reconstructed document
    chunk* current = head;
    int in_bold = 0;
    int in_italic = 0;
    while (current != NULL) {
        if (current->is_bold && !in_bold && current->format_version < doc->version) {
            fprintf(stream, "**");
            in_bold = 1;
        } else if (!current->is_bold && in_bold && current->format_version < doc->version) {
            fprintf(stream, "**");
            in_bold = 0;
        }
        if (current->is_italic && !in_italic && current->format_version < doc->version) {
            fprintf(stream, "*");
            in_italic = 1;
        } else if (!current->is_italic && in_italic && current->format_version < doc->version) {
            fprintf(stream, "*");
            in_italic = 0;
        }
        fputc(current->data, stream);
        current = current->next;
    }
    if (in_italic) {
        fprintf(stream, "*");
    }
    if (in_bold) {
        fprintf(stream, "**");
    }
    // Free temporary chunks
    current = head;
    while (current != NULL) {
        chunk* temp = current;
        current = current->next;
        free(temp);
    }
    // Free operations list
    op = ops_list;
    while (op != NULL) {
        operation* temp = op;
        op = op->next;
        if (temp->content != NULL) {
            free(temp->content);
        }
        free(temp);
    }
}

// 7.1 Utility: Flatten document to string
char *markdown_flatten(const document *doc) {
    // Check for NULL document
    if (doc == NULL) {
        return NULL;
    }
    // First, collect all operations in a list (reverse order, newest first)
    operation* ops_list = NULL;
    operation* op = doc->ops;
    while (op != NULL) {
        operation* new_op = malloc(sizeof(operation));
        *new_op = *op;
        new_op->next = ops_list;
        if (op->content != NULL) {
            new_op->content = malloc(strlen(op->content) + 1);
            strcpy(new_op->content, op->content);
        }
        ops_list = new_op;
        op = op->next;
    }
    // Build document state up to version - 1
    chunk* head = NULL;
    size_t length = 0;
    // Apply operations in order (newest first for correct insertion order at same position)
    op = ops_list;
    while (op != NULL) {
        if (op->version >= doc->version) {
            op = op->next;
            continue;
        }
        if (op->type == OP_INSERT) {
            chunk* new_chunk = NULL;
            chunk* prev = NULL;
            chunk* current = head;
            size_t i = 0;
            while (current != NULL && i < op->pos) {
                prev = current;
                current = current->next;
                i++;
            }
            for (size_t j = 0; j < op->len; j++) {
                new_chunk = malloc(sizeof(chunk));
                new_chunk->data = op->content[j];
                new_chunk->is_bold = 0;
                new_chunk->is_italic = 0;
                new_chunk->version = op->version;
                new_chunk->format_version = op->version;
                new_chunk->formatting = NONE;
                new_chunk->next = current;
                if (prev == NULL) {
                    head = new_chunk;
                } else {
                    prev->next = new_chunk;
                }
                prev = new_chunk;
                current = new_chunk->next;
                length++;
            }
        } else if (op->type == OP_DELETE) {
            chunk* prev = NULL;
            chunk* current = head;
            size_t i = 0;
            while (current != NULL && i < op->pos) {
                prev = current;
                current = current->next;
                i++;
            }
            for (size_t j = 0; j < op->len && current != NULL; j++) {
                chunk* to_delete = current;
                current = current->next;
                if (prev == NULL) {
                    head = current;
                } else {
                    prev->next = current;
                }
                free(to_delete);
                length--;
            }
        } else if (op->type == OP_BOLD || op->type == OP_ITALIC) {
            chunk* current = head;
            size_t i = 0;
            while (current != NULL && i < length) {
                if (i >= op->pos && i < op->pos + op->len) {
                    if (op->type == OP_BOLD) {
                        current->is_bold = 1;
                        current->format_version = op->version;
                    } else if (op->type == OP_ITALIC) {
                        current->is_italic = 1;
                        current->format_version = op->version;
                    }
                }
                current = current->next;
                i++;
            }
        }
        op = op->next;
    }
    // Flatten the reconstructed document
    char* result = malloc(length * 6 + 1);
    if (result == NULL) {
        return NULL;
    }
    chunk* current = head;
    int in_bold = 0;
    int in_italic = 0;
    size_t pos = 0;
    while (current != NULL) {
        if (current->is_bold && !in_bold && current->format_version < doc->version) {
            result[pos++] = '*'; result[pos++] = '*';
            in_bold = 1;
        } else if (!current->is_bold && in_bold && current->format_version < doc->version) {
            result[pos++] = '*'; result[pos++] = '*';
            in_bold = 0;
        }
        if (current->is_italic && !in_italic && current->format_version < doc->version) {
            result[pos++] = '*';
            in_italic = 1;
        } else if (!current->is_italic && in_italic && current->format_version < doc->version) {
            result[pos++] = '*';
            in_italic = 0;
        }
        result[pos++] = current->data;
        current = current->next;
    }
    if (in_italic) {
        result[pos++] = '*';
    }
    if (in_bold) {
        result[pos++] = '*'; result[pos++] = '*';
    }
    result[pos] = '\0';
    // Free temporary chunks
    current = head;
    while (current != NULL) {
        chunk* temp = current;
        current = current->next;
        free(temp);
    }
    // Free operations list
    op = ops_list;
    while (op != NULL) {
        operation* temp = op;
        op = op->next;
        if (temp->content != NULL) {
            free(temp->content);
        }
        free(temp);
    }
    return result;
}

// === Versioning ===
void markdown_increment_version(document *doc) {
    // Check for NULL document
    if (doc == NULL) {
        return;
    }
    doc->version++;
}