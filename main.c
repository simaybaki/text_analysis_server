#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h> // For boolean operations

int INPUT_CHARACTER_LIMIT = 100;
int OUTPUT_CHARACTER_LIMIT = 200;
int PORT_NUMBER = 60000;
int LEVENSHTEIN_LIST_LIMIT = 5;

// Define constants
#define WORD_LENGTH 50

pthread_mutex_t telnet_mutex; // Mutex for synchronized Telnet communication

void file_operations(const char *dictionary_file, char ***words, int *word_count);
char **process_input(int *word_count, const char *input);
void start_server(int port_number);
void handle_client(int client_fd);

// Define the WordDistance structure
typedef struct {
    char *word;
    size_t distance;
} WordDistance;

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

void file_operations(const char *dictionary_file, char ***words, int *word_count) {
    FILE *file;
    char buffer[WORD_LENGTH];
    int capacity = 10;
    *words = (char **)malloc(capacity * sizeof(char *));
    if (*words == NULL) {
        fprintf(stderr, "ERROR: Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    file = fopen(dictionary_file, "r");
    if (file == NULL) {
        fprintf(stderr, "ERROR: Dictionary file \"%s\" not found!\n", dictionary_file);
        free(*words);
        exit(EXIT_FAILURE);
    }

    *word_count = 0;
    while (fscanf(file, "%49s", buffer) != EOF) {
        if (*word_count >= capacity) {
            capacity *= 2;
            *words = (char **)realloc(*words, capacity * sizeof(char *));
            if (*words == NULL) {
                fprintf(stderr, "ERROR: Memory reallocation failed.\n");
                fclose(file);
                exit(EXIT_FAILURE);
            }
        }

        (*words)[*word_count] = (char *)malloc((strlen(buffer) + 1) * sizeof(char));
        if ((*words)[*word_count] == NULL) {
            fprintf(stderr, "ERROR: Memory allocation failed for word.\n");
            fclose(file);
            exit(EXIT_FAILURE);
        }
        strcpy((*words)[*word_count], buffer);
        (*word_count)++;
    }

    fclose(file);
}

char **process_input(int *word_count, const char *input) {
    char *processed_input;
    char **words = NULL;
    int i, j = 0;
    processed_input = (char *)malloc((strlen(input) + 1) * sizeof(char));
    if (processed_input == NULL) {
        fprintf(stderr, "ERROR: Memory allocation failed.\n");
        return NULL;
    }

    for (i = 0; input[i] != '\0'; i++) {
        if (isalpha(input[i]) || isspace(input[i])) {
            processed_input[j++] = tolower(input[i]);
        }
    }
    processed_input[j] = '\0';

    char *token = strtok(processed_input, " ");
    while (token != NULL) {
        words = (char **)realloc(words, (*word_count + 1) * sizeof(char *));
        if (words == NULL) {
            fprintf(stderr, "ERROR: Memory reallocation failed.\n");
            free(processed_input);
            return NULL;
        }

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

    free(processed_input);
    return words;
}

void find_closest_words(const char *input_word, char **dictionary_words, int dictionary_size, char **closest_word, int *is_word_found, int client_fd) {
    typedef struct {
        char *word;
        size_t distance;
    } WordDistance;

    WordDistance closest[LEVENSHTEIN_LIST_LIMIT];
    for (int i = 0; i < LEVENSHTEIN_LIST_LIMIT; i++) {
        closest[i].word = NULL;
        closest[i].distance = SIZE_MAX;
    }

    for (int i = 0; i < dictionary_size; i++) {
        size_t distance = levenshtein(input_word, dictionary_words[i]);
        if (distance == 0) {
            *is_word_found = 1;
        }

        for (int j = 0; j < LEVENSHTEIN_LIST_LIMIT; j++) {
            if (distance < closest[j].distance) {
                for (int k = LEVENSHTEIN_LIST_LIMIT - 1; k > j; k--) {
                    closest[k] = closest[k - 1];
                }
                closest[j].word = dictionary_words[i];
                closest[j].distance = distance;
                break;
            }
        }
    }

    if (closest[0].word != NULL) {
        *closest_word = closest[0].word;
    }

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
    char *closest_word;
    int client_fd;
    int word_position;
} ThreadData;



int main(void) {
    pthread_mutex_init(&telnet_mutex, NULL);
    printf("Sunucu %d portunda başlatılıyor...\n", PORT_NUMBER);
    start_server(PORT_NUMBER);
    pthread_mutex_destroy(&telnet_mutex);
}

void start_server(int port_number) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("ERROR: Failed to create socket");
        exit(EXIT_FAILURE);
    }

    // Set SO_REUSEADDR to allow reuse of the port
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("ERROR: Failed to set socket options");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_number);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("ERROR: Failed to bind socket");
        close(server_fd); // Close the socket to release the port
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) == -1) {
        perror("ERROR: Failed to listen on socket");
        close(server_fd); // Close the socket to release the port
        exit(EXIT_FAILURE);
    }

    printf("Server running on port %d\n", port_number);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            perror("ERROR: Failed to accept connection");
            continue;
        }

        handle_client(client_fd);
    }

    close(server_fd); // Close the server socket when done
}

void *thread_function(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    pthread_mutex_lock(&telnet_mutex);

    // Display the word and its position
    char response_message[1024];
    snprintf(response_message, sizeof(response_message), "\nWORD %02d: %s\n", data->word_position, data->input_word);
    send(data->client_fd, response_message, strlen(response_message), 0);

    // Find closest words
    find_closest_words(data->input_word, data->dictionary_words, data->dictionary_size, &data->closest_word, &data->is_word_found, data->client_fd);

    // If word is not found, ask if the user wants to add it
    if (!data->is_word_found) {
        char not_found_message[1024];
        snprintf(not_found_message, sizeof(not_found_message),
                 "\nThe WORD %s is not present in dictionary. \nDo you want to add this word to dictionary? (y/N): ",
                 data->input_word);
        send(data->client_fd, not_found_message, strlen(not_found_message), 0);

        char buffer[1024];
        int response_received = recv(data->client_fd, buffer, sizeof(buffer) - 1, 0);
        if (response_received > 0) {
            buffer[response_received] = '\0';
            if (buffer[0] == 'y' || buffer[0] == 'Y') {

                data->is_word_found = 1;
                // Update in-memory dictionary
                data->dictionary_words = (char **)realloc(data->dictionary_words, (data->dictionary_size + 1) * sizeof(char *));
                if (data->dictionary_words != NULL) {
                    data->dictionary_words[data->dictionary_size] = (char *)malloc((strlen(data->input_word) + 1) * sizeof(char));
                    if (data->dictionary_words[data->dictionary_size] != NULL) {
                        strcpy(data->dictionary_words[data->dictionary_size], data->input_word);
                        data->dictionary_size++;

                        // Write to dictionary file
                        FILE *file = fopen("basic_english_2000.txt", "a");
                        if (file != NULL) {
                            fprintf(file, "%s\n", data->input_word);
                            fclose(file);
                        }
                        const char *added_message = "The word has been added to the dictionary.\n";
                        send(data->client_fd, added_message, strlen(added_message), 0);
                    }
                }
            } else if (buffer[0] == 'n' || buffer[0] == 'N'){
                const char *skipped_message = "The word has been skipped.\n";
                send(data->client_fd, skipped_message, strlen(skipped_message), 0);
            }else {
                const char *error_message = "ERROR: Invalid input, closing connection...\n";
                send(data->client_fd, error_message, strlen(error_message), 0);
                close(data->client_fd); // Close the client connection
                exit(EXIT_FAILURE); // Shut down the server
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
        return;
    }
    const char *dictionary_file = "basic_english_2000.txt";

    file_operations(dictionary_file, &dictionary_words, &dict_word_count);

    pthread_t threads[input_word_count];
    ThreadData thread_data[input_word_count];

    char corrected_sentence[1024] = ""; // Corrected sentence
    char original_sentence[1024] = ""; // Original sentence

    for (int i = 0; i < input_word_count; i++) {
        strcat(original_sentence, input_words[i]);
        strcat(original_sentence, " "); // Add space between words

        thread_data[i].input_word = input_words[i];
        thread_data[i].dictionary_words = dictionary_words;
        thread_data[i].dictionary_size = dict_word_count;
        thread_data[i].is_word_found = 0;
        thread_data[i].closest_word = NULL; // Initialize closest_word to NULL
        thread_data[i].client_fd = client_fd;
        thread_data[i].word_position = i + 1; // Assign the word position

        if (pthread_create(&threads[i], NULL, thread_function, &thread_data[i]) != 0) {
            fprintf(stderr, "ERROR: Failed to create thread for word %d.\n", i + 1);
            return;
        }
    }

    // Wait for all threads and construct the corrected sentence
    for (int i = 0; i < input_word_count; i++) {
        pthread_join(threads[i], NULL);
        if (!thread_data[i].is_word_found && thread_data[i].closest_word != NULL) {
            strcat(corrected_sentence, thread_data[i].closest_word);
        } else {
            strcat(corrected_sentence, thread_data[i].input_word);
        }
        strcat(corrected_sentence, " ");
    }

    // Send the original and corrected sentences to the client
    char response_message[1024];
    snprintf(response_message, sizeof(response_message), "\nINPUT: %s\n", original_sentence);
    send(client_fd, response_message, strlen(response_message), 0);
    snprintf(response_message, sizeof(response_message), "OUTPUT: %s\n\n", corrected_sentence);
    send(client_fd, response_message, strlen(response_message), 0);

    // Send the farewell message
    const char *farewell_message = "Thank you for using Text Analysis Server! Good Bye!\n";
    send(client_fd, farewell_message, strlen(farewell_message), 0);

    // Free allocated memory
    for (int i = 0; i < input_word_count; i++) {
        free(input_words[i]);
    }
    free(input_words);
    exit(1);
}


void handle_client(int client_fd) {
    const char *welcome_message = "\nType 'exit' to disconnect. Type 'shutdown' to stop the server.\n\nHello, this is Text Analysis Server! \n\nPlease enter your input string:\n";
    send(client_fd, welcome_message, strlen(welcome_message), 0);

    char buffer[1024];

    while (1) {
        int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            printf("Client disconnected.\n");
            break;
        }

        buffer[bytes_received] = '\0';
        printf("Client says: %s", buffer);

        // Shutdown command handling
        if (strncmp(buffer, "shutdown", 8) == 0) {
            const char *shutdown_message = "Shutting down the server...\n";
            send(client_fd, shutdown_message, strlen(shutdown_message), 0);
            close(client_fd);
            exit(0); // Exit the server
        }

        // Exit command handling
        if (strncmp(buffer, "exit", 4) == 0) {
            const char *goodbye_message = "Goodbye!\n";
            send(client_fd, goodbye_message, strlen(goodbye_message), 0);
            break;
        }

        // Remove trailing newline or carriage return
        buffer[strcspn(buffer, "\r\n")] = '\0';

        // Check for input length violation
        if (strlen(buffer) > INPUT_CHARACTER_LIMIT) {
            char error_message[1024];
            snprintf(error_message, sizeof(error_message), "ERROR: Input string is longer than %d characters (INPUT_CHARACTER_LIMIT)!\n", INPUT_CHARACTER_LIMIT);
            send(client_fd, error_message, strlen(error_message), 0);
            close(client_fd); // Close connection
            exit(EXIT_FAILURE); // Shut down the server

        }

        // Check for unsupported characters
        for (int i = 0; buffer[i] != '\0'; i++) {
            if (!isalpha(buffer[i]) && !isspace(buffer[i])) {
                const char *error_message = "ERROR: Input string contains unsupported characters!\n";
                send(client_fd, error_message, strlen(error_message), 0);
                close(client_fd); // Close connection
                exit(EXIT_FAILURE); // Shut down the server
            }
        }

        // Process and send words
        process_and_send_words(client_fd, buffer, NULL, 0);

        close(client_fd); // Close connection after processing the sentence
        return; // End communication
    }

    close(client_fd);
}


