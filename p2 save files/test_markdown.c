#include "../libs/markdown.h"
#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function to print document state to a file
void print_document_state(document* doc, const char* step, FILE* file) {
    fprintf(file, "Step: %s\n", step);
    fprintf(file, "Document Version: %llu\n", (unsigned long long)doc->version);
    fprintf(file, "Document Length: %zu\n", doc->length);
    fprintf(file, "Document Content (via markdown_print): ");
    // markdown_print(doc, file);
    fprintf(file, "\nDocument Content (via markdown_flatten): ");
    char* flattened = markdown_flatten(doc);
    fprintf(file, "%s\n", flattened ? flattened : "NULL");
    free(flattened);
    fprintf(file, "---\n");
}

int main() {
    FILE* file = fopen("test_output.txt", "w");
    if (file == NULL) {
        printf("Error opening file!\n");
        return 1;
    }

    // Test 1: Basic Insert (from spec Section 7.2 Example: Basic Insert)
    fprintf(file, "=== Test 1: Basic Insert ===\n");
    document* doc1 = markdown_init();
    print_document_state(doc1, "After Initialization", file);

    int result = markdown_insert(doc1, 0, 0, "World");
    fprintf(file, "Insert 'World' at pos 0, result: %d\n", result);
    print_document_state(doc1, "After Insert 'World'", file);

    result = markdown_insert(doc1, 0, 0, "Hello ");
    fprintf(file, "Insert 'Hello ' at pos 0, result: %d\n", result);
    print_document_state(doc1, "After Insert 'Hello '", file);

    char* flattened = markdown_flatten(doc1);
    fprintf(file, "Flattened (before commit): %s\n", flattened ? flattened : "NULL");
    free(flattened);

    markdown_increment_version(doc1);
    print_document_state(doc1, "After Version Increment", file);

    flattened = markdown_flatten(doc1);
    fprintf(file, "Flattened (after commit): %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: 'Hello World'\n");
    free(flattened);
    markdown_free(doc1);
    fprintf(file, "---\n");

    // Test 2: Basic Delete
    fprintf(file, "=== Test 2: Basic Delete ===\n");
    document* doc2 = markdown_init();
    print_document_state(doc2, "After Initialization", file);

    result = markdown_insert(doc2, 0, 0, "Hello World");
    fprintf(file, "Insert 'Hello World' at pos 0, result: %d\n", result);
    print_document_state(doc2, "After Insert 'Hello World'", file);

    markdown_increment_version(doc2);
    print_document_state(doc2, "After Version Increment", file);

    result = markdown_delete(doc2, 1, 5, 6);
    fprintf(file, "Delete from pos 5, length 6, result: %d\n", result);
    print_document_state(doc2, "After Delete", file);

    flattened = markdown_flatten(doc2);
    fprintf(file, "Flattened (before next increment): %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: 'Hello World'\n");
    free(flattened);
    markdown_free(doc2);
    fprintf(file, "---\n");

    // Test 3: Format Bold
    fprintf(file, "=== Test 3: Format Bold ===\n");
    document* doc3 = markdown_init();
    print_document_state(doc3, "After Initialization", file);

    result = markdown_insert(doc3, 0, 0, "BoldText");
    fprintf(file, "Insert 'BoldText' at pos 0, result: %d\n", result);
    print_document_state(doc3, "After Insert 'BoldText'", file);

    markdown_increment_version(doc3);
    print_document_state(doc3, "After Version Increment", file);

    result = markdown_bold(doc3, 1, 0, 8);
    fprintf(file, "Bold from pos 0 to 8, result: %d\n", result);
    print_document_state(doc3, "After Bold", file);

    flattened = markdown_flatten(doc3);
    fprintf(file, "Flattened (before next increment): %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: 'BoldText'\n");
    free(flattened);
    markdown_free(doc3);
    fprintf(file, "---\n");

    // Test 4: Basic Deferred
    fprintf(file, "=== Test 4: Basic Deferred ===\n");
    document* doc4 = markdown_init();
    print_document_state(doc4, "After Initialization", file);

    result = markdown_insert(doc4, 0, 0, "Hello, World.");
    fprintf(file, "Insert 'Hello, World.' at pos 0, result: %d\n", result);
    print_document_state(doc4, "After Insert 'Hello, World.'", file);

    markdown_increment_version(doc4);
    print_document_state(doc4, "After Version Increment", file);

    result = markdown_delete(doc4, 1, 0, 12);
    fprintf(file, "Delete 'Hello, World.' at pos 0, length 12, result: %d\n", result);
    print_document_state(doc4, "After Delete", file);

    result = markdown_insert(doc4, 1, 2, "Bar");
    fprintf(file, "Insert 'Bar' at pos 2, result: %d\n", result);
    print_document_state(doc4, "After Insert 'Bar'", file);

    result = markdown_insert(doc4, 1, 1, "Foo");
    fprintf(file, "Insert 'Foo' at pos 1, result: %d\n", result);
    print_document_state(doc4, "After Insert 'Foo'", file);

    flattened = markdown_flatten(doc4);
    fprintf(file, "Flattened: %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: 'Hello, World.'\n");
    free(flattened);
    markdown_free(doc4);
    fprintf(file, "---\n");

    // Test 5: Multiple Inserts at Different Positions
    fprintf(file, "=== Test 5: Multiple Inserts at Different Positions ===\n");
    document* doc5 = markdown_init();
    print_document_state(doc5, "After Initialization", file);

    result = markdown_insert(doc5, 0, 0, "Start");
    fprintf(file, "Insert 'Start' at pos 0, result: %d\n", result);
    print_document_state(doc5, "After Insert 'Start'", file);

    result = markdown_insert(doc5, 0, 5, "Middle");
    fprintf(file, "Insert 'Middle' at pos 5, result: %d\n", result);
    print_document_state(doc5, "After Insert 'Middle'", file);

    result = markdown_insert(doc5, 0, 11, "End");
    fprintf(file, "Insert 'End' at pos 11, result: %d\n", result);
    print_document_state(doc5, "After Insert 'End'", file);

    markdown_increment_version(doc5);
    print_document_state(doc5, "After Version Increment", file);

    flattened = markdown_flatten(doc5);
    fprintf(file, "Flattened: %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: 'StartMiddleEnd'\n");
    free(flattened);
    markdown_free(doc5);
    fprintf(file, "---\n");

    // Test 6: Overlapping Deletes Across Versions
    fprintf(file, "=== Test 6: Overlapping Deletes Across Versions ===\n");
    document* doc6 = markdown_init();
    print_document_state(doc6, "After Initialization", file);

    result = markdown_insert(doc6, 0, 0, "LongTextToDelete");
    fprintf(file, "Insert 'LongTextToDelete' at pos 0, result: %d\n", result);
    print_document_state(doc6, "After Insert 'LongTextToDelete'", file);

    markdown_increment_version(doc6);
    print_document_state(doc6, "After Version Increment to v1", file);

    result = markdown_delete(doc6, 1, 0, 8);
    fprintf(file, "Delete 'LongText' (pos 0, len 8) in v1, result: %d\n", result);
    print_document_state(doc6, "After Delete in v1", file);

    markdown_increment_version(doc6);
    print_document_state(doc6, "After Version Increment to v2", file);

    result = markdown_delete(doc6, 2, 2, 4);
    fprintf(file, "Delete 'Dele' (pos 2, len 4) in v2, result: %d\n", result);
    print_document_state(doc6, "After Delete in v2", file);

    markdown_increment_version(doc6);
    print_document_state(doc6, "After Version Increment to v3", file);

    flattened = markdown_flatten(doc6);
    fprintf(file, "Flattened: %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: 'Tote'\n");
    free(flattened);
    markdown_free(doc6);
    fprintf(file, "---\n");

    // Test 7: Formatting Across Versions
    fprintf(file, "=== Test 7: Formatting Across Versions ===\n");
    document* doc7 = markdown_init();
    print_document_state(doc7, "After Initialization", file);

    result = markdown_insert(doc7, 0, 0, "FormatThisText");
    fprintf(file, "Insert 'FormatThisText' at pos 0, result: %d\n", result);
    print_document_state(doc7, "After Insert 'FormatThisText'", file);

    markdown_increment_version(doc7);
    print_document_state(doc7, "After Version Increment to v1", file);

    result = markdown_bold(doc7, 1, 0, 6);
    fprintf(file, "Bold 'Format' (pos 0 to 6) in v1, result: %d\n", result);
    print_document_state(doc7, "After Bold in v1", file);

    markdown_increment_version(doc7);
    print_document_state(doc7, "After Version Increment to v2", file);

    result = markdown_italic(doc7, 2, 6, 13);
    fprintf(file, "Italic 'ThisText' (pos 6 to 13) in v2, result: %d\n", result);
    print_document_state(doc7, "After Italic in v2", file);

    markdown_increment_version(doc7);
    print_document_state(doc7, "After Version Increment to v3", file);

    flattened = markdown_flatten(doc7);
    fprintf(file, "Flattened: %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: '**Format***ThisText*\n");
    free(flattened);
    markdown_free(doc7);
    fprintf(file, "---\n");

    // Test 8: Edge Case - Empty Document
    fprintf(file, "=== Test 8: Edge Case - Empty Document ===\n");
    document* doc8 = markdown_init();
    print_document_state(doc8, "After Initialization", file);

    markdown_increment_version(doc8);
    print_document_state(doc8, "After Version Increment to v1", file);

    result = markdown_insert(doc8, 1, 0, "NowNotEmpty");
    fprintf(file, "Insert 'NowNotEmpty' at pos 0 in v1, result: %d\n", result);
    print_document_state(doc8, "After Insert in v1", file);

    flattened = markdown_flatten(doc8);
    fprintf(file, "Flattened: %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: ''\n");
    free(flattened);

    markdown_increment_version(doc8);
    print_document_state(doc8, "After Version Increment to v2", file);

    flattened = markdown_flatten(doc8);
    fprintf(file, "Flattened: %s\n", flattened ? flattened : "NULL");
    fprintf(file, "Expected: 'NowNotEmpty'\n");
    free(flattened);
    markdown_free(doc8);
    fprintf(file, "---\n");

    // Test 9: Edge Case - Large Insertion
    fprintf(file, "=== Test 9: Edge Case - Large Insertion ===\n");
    document* doc9 = markdown_init();
    print_document_state(doc9, "After Initialization", file);

    char large_text[1001];
    memset(large_text, 'A', 1000);
    large_text[1000] = '\0';
    result = markdown_insert(doc9, 0, 0, large_text);
    fprintf(file, "Insert 1000 'A's at pos 0, result: %d\n", result);
    print_document_state(doc9, "After Insert Large Text", file);

    markdown_increment_version(doc9);
    print_document_state(doc9, "After Version Increment", file);

    flattened = markdown_flatten(doc9);
    fprintf(file, "Flattened length: %zu\n", strlen(flattened));
    fprintf(file, "Expected length: 1000\n");
    free(flattened);
    markdown_free(doc9);
    fprintf(file, "---\n");

    // Test 10: Edge Case - Invalid Cursor Position
    fprintf(file, "=== Test 10: Edge Case - Invalid Cursor Position ===\n");
    document* doc10 = markdown_init();
    print_document_state(doc10, "After Initialization", file);

    result = markdown_insert(doc10, 0, 100, "ShouldFail");
    fprintf(file, "Insert 'ShouldFail' at pos 100, result: %d\n", result);
    print_document_state(doc10, "After Invalid Insert", file);

    markdown_free(doc10);
    fprintf(file, "---\n");

    fclose(file);
    return 0;
}