#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
int INPUT_CHARACTER_LIMIT = 100;
int OUTPUT_CHARACTER_LIMIT = 200;
int PORT_NUMBER = 60000;
int LEVENSHTEIN_LIST_LIMIT = 5;

// Define constants
#define WORD_LENGTH 50

void file_operations(const char *dictionary_file, char ***words, int *word_count);
// Function prototype
char **process_input(int *word_count);


// `levenshtein.c` - levenshtein
// MIT licensed.
// Copyright (c) 2015 Titus Wormer <tituswormer@gmail.com>

#include <string.h>
#include <stdlib.h>
// Returns a size_t, depicting the difference between `a` and `b`.
// See <https://en.wikipedia.org/wiki/Levenshtein_distance> for more information.
size_t
levenshtein_n(const char *a, const size_t length, const char *b, const size_t bLength) {
    // Shortcut optimizations / degenerate cases.
    if (a == b) {
        return 0;
    }

    if (length == 0) {
        return bLength;
    }

    if (bLength == 0) {
        return length;
    }

    size_t *cache = calloc(length, sizeof(size_t));
    size_t index = 0;
    size_t bIndex = 0;
    size_t distance;
    size_t bDistance;
    size_t result;
    char code;

    // initialize the vector.
    while (index < length) {
        cache[index] = index + 1;
        index++;
    }

    // Loop.
    while (bIndex < bLength) {
        code = b[bIndex];
        result = distance = bIndex++;
        index = SIZE_MAX;

        while (++index < length) {
            bDistance = code == a[index] ? distance : distance + 1;
            distance = cache[index];

            cache[index] = result = distance > result
              ? bDistance > result
                ? result + 1
                : bDistance
              : bDistance > distance
                ? distance + 1
                : bDistance;
        }
    }

    free(cache);

    return result;
}

size_t
levenshtein(const char *a, const char *b) {
    const size_t length = strlen(a);
    const size_t bLength = strlen(b);

    return levenshtein_n(a, length, b, bLength);
}

// Function to handle file operations
void file_operations(const char *dictionary_file, char ***words, int *word_count) {
    FILE *file;
    char buffer[WORD_LENGTH];
    int capacity = 10; // Initial capacity for word list

    // Allocate memory for the initial capacity
    *words = (char **)malloc(capacity * sizeof(char *));
    if (*words == NULL) {
        fprintf(stderr, "ERROR: Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    // Open the dictionary file
    file = fopen(dictionary_file, "r");
    if (file == NULL) {
        fprintf(stderr, "ERROR: Dictionary file \"%s\" not found!\n", dictionary_file);
        free(*words);
        exit(EXIT_FAILURE);
    }

    // Read words from the file
    *word_count = 0;
    while (fscanf(file, "%49s", buffer) != EOF) {
        // Check if the current capacity is exceeded
        if (*word_count >= capacity) {
            capacity *= 2; // Double the capacity
            *words = (char **)realloc(*words, capacity * sizeof(char *));
            if (*words == NULL) {
                fprintf(stderr, "ERROR: Memory reallocation failed.\n");
                fclose(file);
                exit(EXIT_FAILURE);
            }
        }

        // Allocate memory for each word and copy it
        (*words)[*word_count] = (char *)malloc((strlen(buffer) + 1) * sizeof(char));
        if ((*words)[*word_count] == NULL) {
            fprintf(stderr, "ERROR: Memory allocation failed for word.\n");
            fclose(file);
            exit(EXIT_FAILURE);
        }
        strcpy((*words)[*word_count], buffer);
        (*word_count)++;
    }

    // Close the file
    fclose(file);
}

// Function to process user input and return an array of words
char **process_input(int *word_count) {
    char input[INPUT_CHARACTER_LIMIT + 1]; // Allow space for null terminator
    char *processed_input;
    char **words = NULL;
    int i, j = 0;

    printf("Enter your input (single word or sentence): ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        fprintf(stderr, "ERROR: Failed to read input.\n");
        return NULL;
    }

    // Remove newline character if present
    input[strcspn(input, "\n")] = '\0';

    // Check if input exceeds character limit
    if (strlen(input) > INPUT_CHARACTER_LIMIT) {
        fprintf(stderr, "ERROR: Input exceeds character limit of %d.\n", INPUT_CHARACTER_LIMIT);
        return NULL;
    }

    // Allocate memory for the processed string
    processed_input = (char *)malloc((strlen(input) + 1) * sizeof(char));
    if (processed_input == NULL) {
        fprintf(stderr, "ERROR: Memory allocation failed.\n");
        return NULL;
    }

    // Remove punctuation and convert to lowercase
    for (i = 0; input[i] != '\0'; i++) {
        if (isalpha(input[i]) || isspace(input[i])) {
            processed_input[j++] = tolower(input[i]);
        }
    }
    processed_input[j] = '\0'; // Null-terminate the processed string

    // Tokenize the processed string into words
    char *token = strtok(processed_input, " ");
    while (token != NULL) {
        // Reallocate memory to store the new word
        words = (char **)realloc(words, (*word_count + 1) * sizeof(char *));
        if (words == NULL) {
            fprintf(stderr, "ERROR: Memory reallocation failed.\n");
            free(processed_input);
            return NULL;
        }

        // Allocate memory for the word and copy it
        words[*word_count] = (char *)malloc((strlen(token) + 1) * sizeof(char));
        if (words[*word_count] == NULL) {
            fprintf(stderr, "ERROR: Memory allocation failed for word.\n");
            free(processed_input);
            for (int k = 0; k < *word_count; k++) {
                free(words[k]);
            }
            free(words);
            return NULL;
        }
        strcpy(words[*word_count], token);
        (*word_count)++;
        token = strtok(NULL, " ");
    }

    // Free the processed input string as it's no longer needed
    free(processed_input);

    return words;
}



int main(void) {
    int word_count = 0;
    char **words = process_input(&word_count);
    const char *dictionary_file = "basic_english_2000.txt"; // File name
    char **dictionary_words = NULL; // Array of words in the file
    int dict_word_count = 0; // Number of words loaded

    // Call file_operations function
    file_operations(dictionary_file, &dictionary_words, &dict_word_count);
    if (words != NULL) {
        printf("Processed Words:\n");
        for (int i = 0; i < word_count; i++) {
            printf("%s\n", words[i]);
            free(words[i]); // Free memory for each word
        }
        free(words); // Free the array of words
    }
    // Print loaded words for verification
    /*printf("Loaded %d words from \"%s\":\n", dict_word_count, dictionary_file);
    for (int i = 0; i < dict_word_count; i++) {
        printf("%s\n", words[i]);
    }*/

    // Free allocated memory
    for (int i = 0; i < dict_word_count; i++) {
        free(dictionary_words[i]);
    }
    free(dictionary_words);
    const char *str1 = "simay";
    const char *str2 = "simay";

    size_t distance = levenshtein(str1, str2);

    printf("Levenshtein distance between '%s' and '%s' is %zu\n", str1, str2, distance);

    return 0;
}