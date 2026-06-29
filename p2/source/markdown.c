#include "../libs/markdown.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define SUCCESS 0
#define INVALID_CURSOR_POS -1
#define OUTDATED_VERSION -3

// Initializes markdown document with operation-based structure
document *markdown_init(void) {
    document *doc = malloc(sizeof(document));
    if (doc) {
        doc->head = NULL;
        doc->length = 0;
        doc->version = 0;
        doc->ops = NULL;
    }
    return doc;
}

// Frees document, releasing chunks and operations
void markdown_free(document *doc) {
    if (!doc) return;

    chunk *current_chunk = doc->head;
    while (current_chunk) {
        chunk *temp = current_chunk;
        current_chunk = current_chunk->next;
        free(temp);
    }

    operation *current_op = doc->ops;
    while (current_op) {
        operation *temp = current_op;
        current_op = current_op->next;
        if (temp->content) free(temp->content);
        free(temp);
    }

    free(doc);
}

// Computes document length by applying operations up to version
size_t compute_committed_length(const document *doc) {
    size_t length = 0;
    operation *ops_list = NULL;

    // Duplicate operations for processing
    for (operation *op = doc->ops; op; op = op->next) {
        operation *new_op = malloc(sizeof(operation));
        *new_op = *op;
        new_op->next = ops_list;
        if (op->content) {
            new_op->content = malloc(strlen(op->content) + 1);
            strcpy(new_op->content, op->content);
        }
        ops_list = new_op;
    }

    // Reverse to process oldest operations first
    operation *ordered_ops = NULL;
    while (ops_list) {
        operation *temp = ops_list;
        ops_list = ops_list->next;
        temp->next = ordered_ops;
        ordered_ops = temp;
    }

    // Adjust length based on insert/delete operations
    for (operation *op = ordered_ops; op; op = op->next) {
        if (op->version > doc->version) continue;
        if (op->type == OP_INSERT) {
            length += op->len;
        } else if (op->type == OP_DELETE) {
            size_t len = op->len;
            if (op->pos + len > length) len = length - op->pos;
            length -= len;
        }
    }

    // Release temporary operations list
    while (ordered_ops) {
        operation *temp = ordered_ops;
        ordered_ops = ordered_ops->next;
        if (temp->content) free(temp->content);
        free(temp);
    }

    return length;
}

// Counts ordered list items before position for numbering
int count_prior_list_items(const document *doc, size_t pos, uint64_t version) {
    int count = 0;
    char *flattened = markdown_flatten(doc);
    size_t len = flattened ? strlen(flattened) : 0;
    size_t i = 0;

    // Scan for list markers (e.g., "1. ")
    while (i < pos && i < len) {
        if (flattened[i] == '\n') {
            if (i + 3 < len && flattened[i + 1] >= '1' && flattened[i + 1] <= '9' &&
                flattened[i + 2] == '.' && flattened[i + 3] == ' ') {
                count++;
                i += 4;
            } else {
                i++;
            }
        } else {
            i++;
        }
    }

    free(flattened);
    return count;
}

// Inserts content at position with version validation
int markdown_insert(document *doc, uint64_t version, size_t pos, const char *content) {
    if (version != doc->version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) pos = committed_length;

    operation *new_op = malloc(sizeof(operation));
    new_op->type = OP_INSERT;
    new_op->version = version;
    new_op->pos = pos;
    new_op->len = strlen(content);
    new_op->content = malloc(new_op->len + 1);
    strcpy(new_op->content, content);
    new_op->next = doc->ops;
    doc->ops = new_op;
    doc->length += new_op->len;
    return SUCCESS;
}

// Deletes content at position with bounds checking
int markdown_delete(document *doc, uint64_t version, size_t pos, size_t len) {
    if (version != doc->version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (pos >= committed_length) return INVALID_CURSOR_POS;

    operation *new_op = malloc(sizeof(operation));
    new_op->type = OP_DELETE;
    new_op->version = version;
    new_op->pos = pos;
    new_op->len = len;
    new_op->content = NULL;
    new_op->next = doc->ops;
    doc->ops = new_op;
    if (pos + len > doc->length) len = doc->length - pos;
    doc->length -= len;
    return SUCCESS;
}

// Inserts newline at position
int markdown_newline(document *doc, uint64_t version, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) pos = committed_length;
    char nl[2] = {'\n', '\0'};
    return markdown_insert(doc, version, pos, nl);
}

// Adds heading prefix (e.g., "# ") with level check
int markdown_heading(document *doc, uint64_t version, size_t level, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    if (level < 1 || level > 3) return INVALID_CURSOR_POS;
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) pos = committed_length;
    if (pos > 0 && markdown_flatten(doc)[pos - 1] != '\n') {
        int result = markdown_insert(doc, version, pos, "\n");
        if (result != SUCCESS) return result;
        pos++;
    }
    char prefix[5] = {0};
    for (size_t i = 0; i < level; i++) prefix[i] = '#';
    prefix[level] = ' ';
    return markdown_insert(doc, version, pos, prefix);
}

// Inserts blockquote marker (> ) with newline check
int markdown_blockquote(document *doc, uint64_t version, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) pos = committed_length;
    if (pos > 0 && markdown_flatten(doc)[pos - 1] != '\n') {
        int result = markdown_insert(doc, version, pos, "\n");
        if (result != SUCCESS) return result;
        pos++;
    }
    return markdown_insert(doc, version, pos, "> ");
}

// Adds ordered list item with dynamic numbering
int markdown_ordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) pos = committed_length;
    if (pos > 0 && markdown_flatten(doc)[pos - 1] != '\n') {
        int result = markdown_insert(doc, version, pos, "\n");
        if (result != SUCCESS) return result;
        pos++;
    }
    // Number list items based on prior items
    int list_number = count_prior_list_items(doc, pos, version) + 1;
    if (list_number > 9) list_number = 9; // Cap at 9 per spec
    char marker[5];
    snprintf(marker, sizeof(marker), "%d. ", list_number);
    return markdown_insert(doc, version, pos, marker);
}

// Inserts unordered list marker (- )
int markdown_unordered_list(document *doc, uint64_t version, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) pos = committed_length;
    if (pos > 0 && markdown_flatten(doc)[pos - 1] != '\n') {
        int result = markdown_insert(doc, version, pos, "\n");
        if (result != SUCCESS) return result;
        pos++;
    }
    return markdown_insert(doc, version, pos, "- ");
}

// Applies code formatting with backticks
int markdown_code(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (start >= committed_length || end > committed_length || start >= end) {
        return INVALID_CURSOR_POS;
    }
    int result = markdown_insert(doc, version, end, "`");
    if (result != SUCCESS) return result;
    return markdown_insert(doc, version, start, "`");
}

// Inserts horizontal rule (---)
int markdown_horizontal_rule(document *doc, uint64_t version, size_t pos) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (pos > committed_length) pos = committed_length;
    if (pos > 0 && markdown_flatten(doc)[pos - 1] != '\n') {
        int result = markdown_insert(doc, version, pos, "\n");
        if (result != SUCCESS) return result;
        pos++;
    }
    return markdown_insert(doc, version, pos, "---\n");
}

// Creates markdown link with [text](url) syntax
int markdown_link(document *doc, uint64_t version, size_t start, size_t end, const char *url) {
    if (!doc || !url) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (start >= committed_length || end > committed_length || start >= end) {
        return INVALID_CURSOR_POS;
    }
    int result = markdown_insert(doc, version, end, ")");
    if (result != SUCCESS) return result;
    result = markdown_insert(doc, version, end, url);
    if (result != SUCCESS) return result;
    result = markdown_insert(doc, version, end, "(");
    if (result != SUCCESS) return result;
    result = markdown_insert(doc, version, end, "]");
    if (result != SUCCESS) return result;
    return markdown_insert(doc, version, start, "[");
}

// Applies bold formatting with **
int markdown_bold(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (start >= committed_length || end > committed_length || start >= end) {
        return INVALID_CURSOR_POS;
    }
    int result = markdown_insert(doc, version, end, "**");
    if (result != SUCCESS) return result;
    return markdown_insert(doc, version, start, "**");
}

// Applies italic formatting with *
int markdown_italic(document *doc, uint64_t version, size_t start, size_t end) {
    if (!doc) return INVALID_CURSOR_POS;
    if (doc->version != version) return OUTDATED_VERSION;
    size_t committed_length = compute_committed_length(doc);
    if (start >= committed_length || end > committed_length || start >= end) {
        return INVALID_CURSOR_POS;
    }
    int result = markdown_insert(doc, version, end, "*");
    if (result != SUCCESS) return result;
    return markdown_insert(doc, version, start, "*");
}

// Renders document to stream by applying operations
void markdown_print(const document *doc, FILE *stream) {
    if (!doc || !stream) return;

    // Build oldest-first operations list
    operation *ops_list = NULL;
    for (operation *op = doc->ops; op; op = op->next) {
        operation *new_op = malloc(sizeof(operation));
        *new_op = *op;
        new_op->next = ops_list;
        if (op->content) {
            new_op->content = malloc(strlen(op->content) + 1);
            strcpy(new_op->content, op->content);
        }
        ops_list = new_op;
    }

    // Construct chunk list from operations
    chunk *head = NULL;
    size_t length = 0;
    for (operation *op = ops_list; op; op = op->next) {
        if (op->version >= doc->version) continue;

        if (op->type == OP_INSERT) {
            chunk *new_chunk = NULL;
            chunk *prev = NULL;
            chunk *current = head;
            size_t i = 0;
            size_t pos = op->pos > length ? length : op->pos;
            while (current && i < pos) {
                prev = current;
                current = current->next;
                i++;
            }
            for (size_t j = 0; j < op->len; j++) {
                new_chunk = malloc(sizeof(chunk));
                new_chunk->data = op->content[j];
                new_chunk->version = op->version;
                new_chunk->formatting = NONE;
                new_chunk->next = current;
                if (!prev) head = new_chunk;
                else prev->next = new_chunk;
                prev = new_chunk;
                current = new_chunk->next;
                length++;
            }
        } else if (op->type == OP_DELETE) {
            chunk *prev = NULL;
            chunk *current = head;
            size_t i = 0;
            size_t pos = op->pos > length ? length : op->pos;
            while (current && i < pos) {
                prev = current;
                current = current->next;
                i++;
            }
            size_t delete_len = op->len;
            if (pos + delete_len > length) delete_len = length - pos;
            for (size_t j = 0; j < delete_len && current; j++) {
                chunk *to_delete = current;
                current = current->next;
                if (!prev) head = current;
                else prev->next = current;
                free(to_delete);
                length--;
            }
        }
    }

    // Output document content
    for (chunk *current = head; current; current = current->next) {
        fputc(current->data, stream);
    }

    // Release temporary structures
    while (head) {
        chunk *temp = head;
        head = head->next;
        free(temp);
    }
    while (ops_list) {
        operation *temp = ops_list;
        ops_list = ops_list->next;
        if (temp->content) free(temp->content);
        free(temp);
    }
}

// Flattens document to string by replaying operations
char *markdown_flatten(const document *doc) {
    if (!doc) return NULL;

    // Build oldest-first operations list
    operation *ops_list = NULL;
    for (operation *op = doc->ops; op; op = op->next) {
        operation *new_op = malloc(sizeof(operation));
        *new_op = *op;
        new_op->next = ops_list;
        if (op->content) {
            new_op->content = malloc(strlen(op->content) + 1);
            strcpy(new_op->content, op->content);
        }
        ops_list = new_op;
    }

    // Construct chunk list from operations
    chunk *head = NULL;
    size_t length = 0;
    for (operation *op = ops_list; op; op = op->next) {
        if (op->version >= doc->version) continue;

        if (op->type == OP_INSERT) {
            chunk *new_chunk = NULL;
            chunk *prev = NULL;
            chunk *current = head;
            size_t i = 0;
            size_t pos = op->pos > length ? length : op->pos;
            while (current && i < pos) {
                prev = current;
                current = current->next;
                i++;
            }
            for (size_t j = 0; j < op->len; j++) {
                new_chunk = malloc(sizeof(chunk));
                new_chunk->data = op->content[j];
                new_chunk->version = op->version;
                new_chunk->formatting = NONE;
                new_chunk->next = current;
                if (!prev) head = new_chunk;
                else prev->next = new_chunk;
                prev = new_chunk;
                current = new_chunk->next;
                length++;
            }
        } else if (op->type == OP_DELETE) {
            chunk *prev = NULL;
            chunk *current = head;
            size_t i = 0;
            size_t pos = op->pos > length ? length : op->pos;
            while (current && i < pos) {
                prev = current;
                current = current->next;
                i++;
            }
            size_t delete_len = op->len;
            if (pos + delete_len > length) delete_len = length - pos;
            for (size_t j = 0; j < delete_len && current; j++) {
                chunk *to_delete = current;
                current = current->next;
                if (!prev) head = current;
                else prev->next = current;
                free(to_delete);
                length--;
            }
        }
    }

    // Allocate and build result string
    char *result = malloc(length + 1);
    if (!result) return NULL;

    size_t pos = 0;
    for (chunk *current = head; current; current = current->next) {
        result[pos++] = current->data;
    }
    result[pos] = '\0';

    // Release temporary structures
    while (head) {
        chunk *temp = head;
        head = head->next;
        free(temp);
    }
    while (ops_list) {
        operation *temp = ops_list;
        ops_list = ops_list->next;
        if (temp->content) free(temp->content);
        free(temp);
    }

    return result;
}

// Increments document version for update tracking
void markdown_increment_version(document *doc) {
    if (!doc) return;
    doc->version++;
}