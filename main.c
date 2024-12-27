#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <pthread.h> // Include pthread library
#include <arpa/inet.h>
#include <unistd.h>

int INPUT_CHARACTER_LIMIT = 100;
int OUTPUT_CHARACTER_LIMIT = 200;
int PORT_NUMBER = 60000;
int LEVENSHTEIN_LIST_LIMIT = 5;

// Define constants
#define WORD_LENGTH 50

pthread_mutex_t telnet_mutex; // Mutex for synchronized Telnet communication

void file_operations(const char *dictionary_file, char ***words, int *word_count);
// Function prototype
char **process_input(int *word_count, const char *input);
void start_server(int port_number);
void handle_client(int client_fd);

// Define the WordDistance structure
typedef struct {
    char *word;
    size_t distance;
} WordDistance;

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
char **process_input(int *word_count, const char *input) {
    char *processed_input;
    char **words = NULL;
    int i, j = 0;

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

    // Print the words for debugging
    printf("Processed words:\n");
    for (int k = 0; k < *word_count; k++) {
        printf("Word %d: %s\n", k + 1, words[k]);
    }

    return words;
}

// Function to find the top LEVENSHTEIN_LIST_LIMIT closest words and their distances
void find_closest_words(const char *input_word, char **dictionary_words, int dictionary_size, int *is_word_found, int client_fd) {
    typedef struct {
        char *word;
        size_t distance;
    } WordDistance;

    WordDistance closest[LEVENSHTEIN_LIST_LIMIT];
    for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT; i++) {
        closest[i].word = NULL;
        closest[i].distance = SIZE_MAX;
    }

    // Calculate Levenshtein distances and update the closest words list.
    for (int i = 0; i < dictionary_size; i++) {
        size_t distance = levenshtein(input_word, dictionary_words[i]);

        if (distance == 0) {
            *is_word_found = 1;
        }

        // Check if this word is closer than the farthest word in the list
        for (int j = 0; j < LEVENSHTEIN_LIST_LIMIT; j++) {
            if (distance < closest[j].distance) {
                // Shift remaining entries to make room for the new word
                for (int k = LEVENSHTEIN_LIST_LIMIT - 1; k > j; k--) {
                    closest[k] = closest[k - 1];
                }
                closest[j].word = dictionary_words[i];
                closest[j].distance = distance;
                break;
            }
        }
    }

    // Send the closest matches to the client
    char message[1024];
    snprintf(message, sizeof(message), "MATCHES: ");
    send(client_fd, message, strlen(message), 0);
    for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT && closest[i].word != NULL; i++) {
        snprintf(message, sizeof(message), "%s (%zu) ", closest[i].word, closest[i].distance);
        send(client_fd, message, strlen(message), 0);
    }
    snprintf(message, sizeof(message), "\n");
    send(client_fd, message, strlen(message), 0);
}

typedef struct {
    char *input_word;
    char **dictionary_words;
    int dictionary_size;
    int is_word_found;
    int client_fd;
} ThreadData;

void *thread_function(void *arg) {
    ThreadData *data = (ThreadData *)arg;

    // Lock Telnet communication to ensure sequential output
    pthread_mutex_lock(&telnet_mutex);

    char response_message[1024];
    snprintf(response_message, sizeof(response_message), "WORD: %s\n", data->input_word);
    send(data->client_fd, response_message, strlen(response_message), 0);

    find_closest_words(data->input_word, data->dictionary_words, data->dictionary_size, &data->is_word_found, data->client_fd);

    if (!data->is_word_found) {
        const char *not_found_message = "The word is not in the dictionary. Would you like to add it? (y/n): ";
        send(data->client_fd, not_found_message, strlen(not_found_message), 0);

        char buffer[1024];
        int response_received = recv(data->client_fd, buffer, sizeof(buffer) - 1, 0);
        if (response_received > 0) {
            buffer[response_received] = '\0';
            if (buffer[0] == 'y' || buffer[0] == 'Y') {
                // Add the word to the dictionary (placeholder, as dynamic dictionary update is not implemented)
                const char *added_message = "The word has been added to the dictionary.\n";
                send(data->client_fd, added_message, strlen(added_message), 0);
            } else {
                const char *skipped_message = "The word has been skipped.\n";
                send(data->client_fd, skipped_message, strlen(skipped_message), 0);
            }
        }
    }

    pthread_mutex_unlock(&telnet_mutex);
    return NULL;
}

void process_and_send_words(int client_fd, const char *input, char **dictionary_words, int dict_word_count) {
    int input_word_count = 0;
    char **input_words = process_input(&input_word_count, input);
    if (input_words == NULL) {
        return; // Exit on error
    }
    const char *dictionary_file = "basic_english_2000.txt";

    file_operations(dictionary_file, &dictionary_words, &dict_word_count);

    pthread_t threads[input_word_count];
    ThreadData thread_data[input_word_count];

    for (int i = 0; i < input_word_count; i++) {
        thread_data[i].input_word = input_words[i];
        thread_data[i].dictionary_words = dictionary_words;
        thread_data[i].dictionary_size = dict_word_count;
        thread_data[i].is_word_found = 0;
        thread_data[i].client_fd = client_fd;

        if (pthread_create(&threads[i], NULL, thread_function, &thread_data[i]) != 0) {
            fprintf(stderr, "ERROR: Failed to create thread for word %d.\n", i + 1);
            return;
        }
    }

    for (int i = 0; i < input_word_count; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < input_word_count; i++) {
        free(input_words[i]);
    }
    free(input_words);
}

int main(void) {
    pthread_mutex_init(&telnet_mutex, NULL); // Initialize the mutex
    printf("Sunucu %d portunda başlatılıyor...\n", PORT_NUMBER);
    start_server(PORT_NUMBER);
    pthread_mutex_destroy(&telnet_mutex); // Destroy the mutex
}

void start_server(int port_number) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("HATA: Soket oluşturulamadı");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_number);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("HATA: Soket bağlanamadı");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) == -1) {
        perror("HATA: Soket dinlenemedi");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Sunucu %d portunda çalışıyor\n", port_number);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            perror("HATA: Bağlantı kabul edilemedi");
            continue;
        }

        handle_client(client_fd);
    }

    close(server_fd);
}

void handle_client(int client_fd) {
    const char *welcome_message = "Merhaba! Sunucuya bağlandınız. 'exit' yazarak bağlantıyı kesebilirsiniz. 'shutdown' yazarak sunucuyu durdurabilirsiniz.\n";
    send(client_fd, welcome_message, strlen(welcome_message), 0);

    char buffer[1024];

    while (1) {
        int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            printf("İstemci bağlantısı kesildi.\n");
            break;
        }

        buffer[bytes_received] = '\0';
        printf("İstemci diyor ki: %s", buffer);

        if (strncmp(buffer, "shutdown", 8) == 0) {
            const char *shutdown_message = "Sunucu kapatılıyor...\n";
            send(client_fd, shutdown_message, strlen(shutdown_message), 0);
            close(client_fd);
            exit(0);
        }

        if (strncmp(buffer, "exit", 4) == 0) {
            const char *goodbye_message = "Güle güle!\n";
            send(client_fd, goodbye_message, strlen(goodbye_message), 0);
            break;
        }

        buffer[strcspn(buffer, "\r\n")] = '\0';
        process_and_send_words(client_fd, buffer, NULL, 0);

        const char *response = "Mesaj alındı!\n";
        send(client_fd, response, strlen(response), 0);
    }

    close(client_fd);
}