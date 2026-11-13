#define _POSIX_C_SOURCE 200809L
#define BANKER_STAGE 3

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    COMMAND_REQUEST,
    COMMAND_RELEASE,
    COMMAND_PRINT
} CommandType;

typedef struct {
    CommandType type;
    size_t customer;
    long *values;
} Command;

typedef struct {
    size_t customers;
    size_t resources;
    long *maximum;
    long *allocation;
    long *need;
    long *available;
} Banker;

static void fail(const char *message)
{
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

static void *checked_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) {
        fail("memory size overflow");
    }

    void *memory = calloc(count, size);
    if (memory == NULL) {
        fail("unable to allocate memory");
    }
    return memory;
}

static void *checked_realloc(void *memory, size_t size)
{
    void *resized = realloc(memory, size);
    if (resized == NULL && size != 0) {
        free(memory);
        fail("unable to allocate memory");
    }
    return resized;
}

static long parse_nonnegative(const char *text, const char *context)
{
    if (text == NULL || *text == '\0') {
        fail(context);
    }

    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < 0) {
        fail(context);
    }
    return value;
}

static void remove_line_ending(char *line)
{
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
}

static size_t parse_csv_values(const char *line, long **values_out)
{
    size_t capacity = 4;
    size_t count = 0;
    long *values = checked_calloc(capacity, sizeof(*values));
    const char *cursor = line;

    for (;;) {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            free(values);
            fail("customer.txt contains an empty or incomplete row");
        }

        errno = 0;
        char *end = NULL;
        long value = strtol(cursor, &end, 10);
        if (errno == ERANGE || end == cursor || value < 0) {
            free(values);
            fail("customer.txt contains an invalid resource value");
        }

        while (isspace((unsigned char)*end)) {
            end++;
        }

        if (count == capacity) {
            capacity *= 2;
            values = checked_realloc(values, capacity * sizeof(*values));
        }
        values[count++] = value;

        if (*end == '\0') {
            break;
        }
        if (*end != ',') {
            free(values);
            fail("customer.txt must use comma-separated integers");
        }
        cursor = end + 1;
    }

    *values_out = values;
    return count;
}

static Banker load_customers(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fail("customer.txt could not be opened");
    }

    long *maximum = NULL;
    size_t customers = 0;
    size_t resources = 0;
    char *line = NULL;
    size_t line_capacity = 0;

    while (getline(&line, &line_capacity, file) != -1) {
        remove_line_ending(line);
        long *row = NULL;
        size_t row_resources = parse_csv_values(line, &row);

        if (resources == 0) {
            resources = row_resources;
        } else if (row_resources != resources) {
            free(row);
            free(line);
            free(maximum);
            fclose(file);
            fail("customer.txt rows have different resource counts");
        }

        if (customers == SIZE_MAX / resources) {
            free(row);
            free(line);
            free(maximum);
            fclose(file);
            fail("customer.txt is too large");
        }

        maximum = checked_realloc(maximum, (customers + 1) * resources * sizeof(*maximum));
        memcpy(maximum + customers * resources, row, resources * sizeof(*row));
        customers++;
        free(row);
    }

    if (ferror(file)) {
        free(line);
        free(maximum);
        fclose(file);
        fail("customer.txt could not be read");
    }
    free(line);
    fclose(file);

    if (customers == 0 || resources == 0) {
        free(maximum);
        fail("customer.txt does not contain customers");
    }

    Banker banker = {
        .customers = customers,
        .resources = resources,
        .maximum = maximum,
        .allocation = checked_calloc(customers * resources, sizeof(long)),
        .need = checked_calloc(customers * resources, sizeof(long)),
        .available = checked_calloc(resources, sizeof(long))
    };
    memcpy(banker.need, banker.maximum, customers * resources * sizeof(long));
    return banker;
}

static void free_banker(Banker *banker)
{
    free(banker->maximum);
    free(banker->allocation);
    free(banker->need);
    free(banker->available);
}

static void parse_available(Banker *banker, int argc, char **argv)
{
    if ((size_t)(argc - 1) != banker->resources) {
        fail("the number of command-line resources does not match customer.txt");
    }

    for (size_t resource = 0; resource < banker->resources; resource++) {
        banker->available[resource] = parse_nonnegative(
            argv[resource + 1],
            "command-line resources must be nonnegative integers"
        );
    }
}

static size_t split_words(char *line, char ***words_out)
{
    size_t capacity = 8;
    size_t count = 0;
    char **words = checked_calloc(capacity, sizeof(*words));
    char *save = NULL;

    for (char *word = strtok_r(line, " \t", &save); word != NULL; word = strtok_r(NULL, " \t", &save)) {
        if (count == capacity) {
            capacity *= 2;
            words = checked_realloc(words, capacity * sizeof(*words));
        }
        words[count++] = word;
    }

    *words_out = words;
    return count;
}

static Command *load_commands(const char *path, const Banker *banker, size_t *count_out)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fail("commands.txt could not be opened");
    }

    size_t capacity = 16;
    size_t count = 0;
    Command *commands = checked_calloc(capacity, sizeof(*commands));
    char *line = NULL;
    size_t line_capacity = 0;

    while (getline(&line, &line_capacity, file) != -1) {
        remove_line_ending(line);
        char **words = NULL;
        size_t words_count = split_words(line, &words);

        if (words_count == 0) {
            free(words);
            free(line);
            fclose(file);
            free(commands);
            fail("commands.txt contains an empty line");
        }

        Command command = {0};
        if (strcmp(words[0], "*") == 0) {
            if (words_count != 1) {
                free(words);
                free(line);
                fclose(file);
                free(commands);
                fail("the * command cannot have arguments");
            }
            command.type = COMMAND_PRINT;
        } else if (strcmp(words[0], "RQ") == 0 || strcmp(words[0], "RL") == 0) {
            if (words_count != banker->resources + 2) {
                free(words);
                free(line);
                fclose(file);
                free(commands);
                fail("a command has the wrong number of resource values");
            }

            long customer = parse_nonnegative(words[1], "a command contains an invalid customer index");
            if ((size_t)customer >= banker->customers) {
                free(words);
                free(line);
                fclose(file);
                free(commands);
                fail("a command references a customer that does not exist");
            }

            command.type = strcmp(words[0], "RQ") == 0 ? COMMAND_REQUEST : COMMAND_RELEASE;
            command.customer = (size_t)customer;
            command.values = checked_calloc(banker->resources, sizeof(long));
            for (size_t resource = 0; resource < banker->resources; resource++) {
                command.values[resource] = parse_nonnegative(
                    words[resource + 2],
                    "a command contains an invalid resource value"
                );
            }
        } else {
            free(words);
            free(line);
            fclose(file);
            free(commands);
            fail("commands.txt contains an unknown command");
        }

        if (count == capacity) {
            capacity *= 2;
            commands = checked_realloc(commands, capacity * sizeof(*commands));
        }
        commands[count++] = command;
        free(words);
    }

    if (ferror(file)) {
        free(line);
        fclose(file);
        for (size_t index = 0; index < count; index++) {
            free(commands[index].values);
        }
        free(commands);
        fail("commands.txt could not be read");
    }
    free(line);
    fclose(file);

    if (count == 0) {
        free(commands);
        fail("commands.txt does not contain commands");
    }

    *count_out = count;
    return commands;
}

static void free_commands(Command *commands, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        free(commands[index].values);
    }
    free(commands);
}

static size_t number_width(long value)
{
    size_t width = 1;
    while (value >= 10) {
        value /= 10;
        width++;
    }
    return width;
}

static size_t vector_width(const long *values, size_t resources)
{
    size_t width = 0;
    for (size_t resource = 0; resource < resources; resource++) {
        width += number_width(values[resource]) + 1;
    }
    return width;
}

static void write_vector(FILE *output, const long *values, size_t resources, bool trailing_space)
{
    for (size_t resource = 0; resource < resources; resource++) {
        if (resource > 0) {
            fputc(' ', output);
        }
        fprintf(output, "%ld", values[resource]);
    }
    if (trailing_space) {
        fputc(' ', output);
    }
}

static void write_padding(FILE *output, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        fputc(' ', output);
    }
}

static void write_state(FILE *output, const Banker *banker)
{
    fputs("MAXIMUM   |   ALLOCATION  |   NEED\n", output);
    for (size_t customer = 0; customer < banker->customers; customer++) {
        const long *maximum = banker->maximum + customer * banker->resources;
        const long *allocation = banker->allocation + customer * banker->resources;
        const long *need = banker->need + customer * banker->resources;

        size_t maximum_width = vector_width(maximum, banker->resources);
        size_t allocation_width = vector_width(allocation, banker->resources);
        write_vector(output, maximum, banker->resources, true);
        write_padding(output, maximum_width < 10 ? 10 - maximum_width : 1);
        fputs("|   ", output);
        write_vector(output, allocation, banker->resources, true);
        write_padding(output, allocation_width < 12 ? 12 - allocation_width : 1);
        fputs("|   ", output);
        write_vector(output, need, banker->resources, true);
        fputc('\n', output);
    }

    fputs("AVAILABLE ", output);
    write_vector(output, banker->available, banker->resources, true);
    fputc('\n', output);
}

static bool is_safe(const Banker *banker)
{
    long *work = checked_calloc(banker->resources, sizeof(long));
    bool *finished = checked_calloc(banker->customers, sizeof(bool));
    memcpy(work, banker->available, banker->resources * sizeof(long));
    size_t completed = 0;

    while (completed < banker->customers) {
        bool progressed = false;
        for (size_t customer = 0; customer < banker->customers; customer++) {
            if (finished[customer]) {
                continue;
            }

            bool possible = true;
            for (size_t resource = 0; resource < banker->resources; resource++) {
                size_t offset = customer * banker->resources + resource;
                if (banker->need[offset] > work[resource]) {
                    possible = false;
                    break;
                }
            }

            if (!possible) {
                continue;
            }

            for (size_t resource = 0; resource < banker->resources; resource++) {
                size_t offset = customer * banker->resources + resource;
                work[resource] += banker->allocation[offset];
            }
            finished[customer] = true;
            completed++;
            progressed = true;
        }

        if (!progressed) {
            break;
        }
    }

    free(work);
    free(finished);
    return completed == banker->customers;
}

static void request_resources(FILE *output, Banker *banker, const Command *command, bool check_safety)
{
    size_t base = command->customer * banker->resources;

    for (size_t resource = 0; resource < banker->resources; resource++) {
        if (command->values[resource] > banker->need[base + resource]) {
            fprintf(output, "The customer %zu request ", command->customer);
            write_vector(output, command->values, banker->resources, false);
            fputs(" was denied because exceed its maximum allocation\n", output);
            return;
        }
    }

    for (size_t resource = 0; resource < banker->resources; resource++) {
        if (command->values[resource] > banker->available[resource]) {
            fputs("The resources ", output);
            write_vector(output, banker->available, banker->resources, true);
            fprintf(output, "are not enough to customer %zu request ", command->customer);
            write_vector(output, command->values, banker->resources, true);
            fputc('\n', output);
            return;
        }
    }

    for (size_t resource = 0; resource < banker->resources; resource++) {
        banker->available[resource] -= command->values[resource];
        banker->allocation[base + resource] += command->values[resource];
        banker->need[base + resource] -= command->values[resource];
    }

    if (check_safety && !is_safe(banker)) {
        for (size_t resource = 0; resource < banker->resources; resource++) {
            banker->available[resource] += command->values[resource];
            banker->allocation[base + resource] -= command->values[resource];
            banker->need[base + resource] += command->values[resource];
        }
        fprintf(output, "The customer %zu request ", command->customer);
        write_vector(output, command->values, banker->resources, false);
        fputs(" was denied because result in an unsafe state\n", output);
        return;
    }

    fprintf(output, "Allocate to customer %zu the resources ", command->customer);
    write_vector(output, command->values, banker->resources, false);
    fputc('\n', output);
}

static void release_resources(FILE *output, Banker *banker, const Command *command)
{
    size_t base = command->customer * banker->resources;
    for (size_t resource = 0; resource < banker->resources; resource++) {
        if (command->values[resource] > banker->allocation[base + resource]) {
            fprintf(output, "The customer %zu cannot release ", command->customer);
            write_vector(output, command->values, banker->resources, false);
            fputs(" because it exceeds its allocation\n", output);
            return;
        }
    }

    for (size_t resource = 0; resource < banker->resources; resource++) {
        banker->available[resource] += command->values[resource];
        banker->allocation[base + resource] -= command->values[resource];
        banker->need[base + resource] += command->values[resource];
    }

    fprintf(output, "Release from customer %zu the resources ", command->customer);
    write_vector(output, command->values, banker->resources, false);
    fputc('\n', output);
}

static void run_commands(FILE *output, Banker *banker, const Command *commands, size_t command_count)
{
    for (size_t index = 0; index < command_count; index++) {
        const Command *command = &commands[index];
        if (command->type == COMMAND_PRINT) {
            write_state(output, banker);
        } else if (command->type == COMMAND_REQUEST && BANKER_STAGE >= 3) {
            request_resources(output, banker, command, BANKER_STAGE >= 4);
        } else if (command->type == COMMAND_RELEASE && BANKER_STAGE >= 5) {
            release_resources(output, banker, command);
        }
    }
}

int main(int argc, char **argv)
{
    Banker banker = load_customers("customer.txt");
    parse_available(&banker, argc, argv);

    if (BANKER_STAGE == 1) {
        free_banker(&banker);
        return EXIT_SUCCESS;
    }

    size_t command_count = 0;
    Command *commands = load_commands("commands.txt", &banker, &command_count);
    FILE *output = fopen("result.txt", "w");
    if (output == NULL) {
        free_commands(commands, command_count);
        free_banker(&banker);
        fail("result.txt could not be created");
    }

    run_commands(output, &banker, commands, command_count);
    if (fclose(output) != 0) {
        free_commands(commands, command_count);
        free_banker(&banker);
        fail("result.txt could not be written");
    }

    free_commands(commands, command_count);
    free_banker(&banker);
    return EXIT_SUCCESS;
}
