#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

const char error_message[] = "An error has occurred\n";

#define MAX_TOKENS 512
#define MAX_PATHS 128

// estructura encargada para la lista de paths
typedef struct {
    char *dirs[MAX_PATHS];
    int count;
} path_list;

// Se encarga de imprimir el mensaje de error esperado
void print_error() {
    write(STDERR_FILENO, error_message, strlen(error_message));
}

/*  limpia los espacios antes y despues de los comandos para que no
	se genere una lectura de espacios en blanco o saltos de linea     */
char *trim(char *s) {
    if (!s) return s;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    return s;
}

/* 	Se encarga de dividir la linea en subcomandos, separados por el caracter '&',
    encargandose a su vez de distribuir memoria y de permitir los comandos en paralelo */
char **split_by_ampersand(char *line, int *out_count) {
    char **res = NULL;
    int cap = 0, n = 0;
    char *saveptr;
    char *token = strtok_r(line, "&", &saveptr);
    while (token) {
        if (n+1 > cap) {
            cap = cap ? cap*2 : 4;
            res = realloc(res, cap * sizeof(char*));
        }
        char *ttrim = trim(token);
        res[n++] = strdup(ttrim);
        token = strtok_r(NULL, "&", &saveptr);
    }
    // terminar 
    if (res) res = realloc(res, (n+1)*sizeof(char*));
    else res = malloc(sizeof(char*));
    res[n] = NULL;
    *out_count = n;
    return res;
}

/*  Convierte la linea de comandos recibida en tokens, que estaran separados por espacios,
	para despues ser leidos por el ejecutador de comandos                                    */
char **tokenize_whitespace(char *cmd, int *out_argc) {
    char **argv = malloc((MAX_TOKENS+1) * sizeof(char*));
    int argc = 0;
    char *saveptr;
    char *t = strtok_r(cmd, " \t\n\r", &saveptr);
    while (t && argc < MAX_TOKENS) {
        argv[argc++] = strdup(t);
        t = strtok_r(NULL, " \t\n\r", &saveptr);
    }
    argv[argc] = NULL;
    *out_argc = argc;
    return argv;
}

//  liberar la memoria que se haya usado en tokenize_whitespace
void free_argv(char **argv) {
    if (!argv) return;
    for (int i=0; argv[i]!=NULL; ++i) free(argv[i]);
    free(argv);
}

//  Inicia el path_list que estara definido como /bin por defecto 
void init_path(path_list *p) {
    p->count = 0;
    p->dirs[0] = strdup("/bin");
    p->count = 1;
}

// reemplaza completamente la lista de path con los nuevos directorios
void set_path(path_list *p, char **argv, int argc) {
    // liberar previos
    for (int i=0; i<p->count; ++i) {
        free(p->dirs[i]);
        p->dirs[i] = NULL;
    }
    p->count = 0;
    for (int i=1; i<argc && p->count < MAX_PATHS; ++i) {
        p->dirs[p->count++] = strdup(argv[i]);
    }
}

// buscar ejecutable de un comando en la ruta path definida, si lo encuentra guarda la ruta en memoria
char *search_in_path(path_list *p, const char *cmd) {
    if (!cmd) return NULL;
    for (int i=0; i<p->count; ++i) {
        size_t len = strlen(p->dirs[i]) + 1 + strlen(cmd) + 1;
        char *candidate = malloc(len);
        snprintf(candidate, len, "%s/%s", p->dirs[i], cmd);
        if (access(candidate, X_OK) == 0) {
            return candidate;
        }
        free(candidate);
    }
    return NULL;
}

// permite la salida del shell, mediante el uso del built-in command exit
int handle_builtin_exit(char **argv, int argc) {
    if (argc > 1) {
        print_error();
        return -1; // error, pero shell continúa
    }
    exit(0);
    return 0;
}

// permite el movimiento entre archivos, mediante el uso del built-in command cd
int handle_builtin_cd(char **argv, int argc) {
    if (argc != 2) {
        print_error();
        return -1;
    }
    if (chdir(argv[1]) != 0) {
        print_error();
        return -1;
    }
    return 0;
}

// permite reemplazar la lista de paths, mediante el built-in command path
int handle_builtin_path(path_list *p, char **argv, int argc) {
    set_path(p, argv, argc);
    return 0;
}

/* Manejo y control de redireccion, dado el caso que '>' exista, se devuelve:
   - retorna 0 en ejecucion exitosa y entrega el output al archivo definido
   - retorna -1 cuando ocurre un error de sintaxis
   - retorna 1 si no se ejecuta redireccion                                      */
int parse_redirection(char **argv, int argc, char ***out_argv_no_redir, int *out_argc_no_redir, char **out_filename) {
    int idx = -1;
    for (int i=0; i<argc; ++i) {
        if (strcmp(argv[i], ">") == 0) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
		// sin redireccion
        *out_argv_no_redir = argv;
        *out_argc_no_redir = argc;
        *out_filename = NULL;
        return 1;
    }
    if (idx == argc-1) {
        // nada despues del '>', por tanto error
        print_error();
        return -1;
    }
    if (idx + 2 < argc) {
        // mas tokens despues del nombre del archivo, dando error
        print_error();
        return -1;
    }
    int nleft = idx;
    char **newargv = malloc((nleft+1) * sizeof(char*));
    for (int i=0; i<nleft; ++i) newargv[i] = strdup(argv[i]);
    newargv[nleft] = NULL;

    *out_argv_no_redir = newargv;
    *out_argc_no_redir = nleft;
    *out_filename = strdup(argv[idx+1]);
    return 0;
}

/* ejecutar un unico comando que no incluye '&':
   - si es un built-in command, lo ejecuta directamente sin necesidad de un fork
   - si es un comando externo, usa el fork() crea un proceso hijo y lo ejecuta
   - si se definio un outfile mediante '>', se redirige el output de stdout y stderr al archivo
   - retorna el process ID, del proceso hijo, si es un built it retorna 0, si es un error retorna -1  */
pid_t execute_single_command(path_list *p, char **argv, int argc, const char *outfile) {
    if (argc == 0) return -1;
    if (argv[0] == NULL) return -1;
    
    /* built-ins: exit, cd, path */
    if (strcmp(argv[0], "exit") == 0) {
        if (outfile) {
            print_error();
            return -1;
        }
        handle_builtin_exit(argv, argc);
        return 0;
    }
    if (strcmp(argv[0], "cd") == 0) {
        if (outfile) {
            print_error();
            return -1;
        }
        handle_builtin_cd(argv, argc);
        return 0;
    }
    if (strcmp(argv[0], "path") == 0) {
        if (outfile) {
            print_error();
            return -1;
        }
        handle_builtin_path(p, argv, argc);
        return 0;
    }

    // buscar ejecutable en el path
    char *exec_path = search_in_path(p, argv[0]);
    if (!exec_path) {
        // si no se encuentra, da error
        print_error();
        return -1;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        print_error();
        free(exec_path);
        return -1;
    } else if (pid == 0) {
        if (outfile) {
            int fd = open(outfile, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
            if (fd < 0) {
                print_error();
                _exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                print_error();
                _exit(1);
            }
            if (dup2(fd, STDERR_FILENO) < 0) {
                print_error();
                _exit(1);
            }
            close(fd);
        }
        char **exec_argv = malloc((argc+1)*sizeof(char*));
        for (int i=0; i<argc; ++i) exec_argv[i] = argv[i];
        exec_argv[argc] = NULL;
        execv(exec_path, exec_argv);
        print_error();
        _exit(1);
    } else {
        free(exec_path);
        return pid;
    }
}

/* Procesa una linea entera, ejecutando los comandos separados por '&' en paralelo, segun las reglas:
   - para todos los subcomandos: lo limpia, tokeniza, manera la redireccion, y ejecuta el comando
   - esperar a que se ejecuten todos los hijos creados por esta linea                                   */
void process_line(char *line, path_list *p) {
    char *linecopy = strdup(line);
    int ncmds = 0;
    char **cmds = split_by_ampersand(linecopy, &ncmds);
    if (ncmds == 0) {
        free(cmds);
        free(linecopy);
        return;
    }

    pid_t pids[ncmds];
    int pid_count = 0;
    int any_error = 0;

    for (int i=0; i<ncmds; ++i) {
        char *cmd_trimmed = trim(cmds[i]);
        if (cmd_trimmed == NULL || strlen(cmd_trimmed) == 0) {
            // comando vacio despues de un '&', genera error
            print_error();
            any_error = 1;
            continue;
        }

        // tokenizar los comandos mediante espaciados
        char *cmddup = strdup(cmd_trimmed);
        int argc_raw = 0;
        char **argv_raw = tokenize_whitespace(cmddup, &argc_raw);
        free(cmddup);

        if (argc_raw == 0) {
            free_argv(argv_raw);
            continue;
        }

        // detectar si hay multiple '>' en tokens
        int count_gt = 0;
        for (int t=0; t<argc_raw; ++t) if (strcmp(argv_raw[t], ">") == 0) count_gt++;
        if (count_gt > 1) {
            print_error();
            free_argv(argv_raw);
            any_error = 1;
            continue;
        }

        char **argv_no_redir = NULL;
        int argc_no_redir = 0;
        char *outfile = NULL;
        int r = parse_redirection(argv_raw, argc_raw, &argv_no_redir, &argc_no_redir, &outfile);
        if (r == -1) {
            // error de sintaxis en la redireccion
            if (r == -1) any_error = 1;
            free_argv(argv_raw);
            continue;
        }

        int created_newargv = 0;
        if (r == 0) {
            created_newargv = 1;
        } else {
            argv_no_redir = argv_raw;
            argc_no_redir = argc_raw;
        }

        // ejecutar un comando built-in
        pid_t childpid = execute_single_command(p, argv_no_redir, argc_no_redir, outfile);
        if (childpid > 0) {
            pids[pid_count++] = childpid;
        } else if (childpid == 0) {
        } else {
            any_error = 1;
        }

        // limpiar memoria utilizada
        if (created_newargv) {
            for (int k=0; k<argc_no_redir; ++k) free(argv_no_redir[k]);
            free(argv_no_redir);
        } else {
            free_argv(argv_no_redir);
        }
        if (outfile) free(outfile);
    }

    // esperar por todos los hijos ejecutados
    for (int i=0; i<pid_count; ++i) {
        int status;
        waitpid(pids[i], &status, 0);
    }

    // liberar memoria de los comandos
    for (int i=0; i<ncmds; ++i) free(cmds[i]);
    free(cmds);
    free(linecopy);
}

// el main se encarga de manejar los argumentos, y los modos interactivo y batch
int main(int argc, char *argv[]) {
    // valida si se ejecuta modo interactivo o batch
    if (argc > 2) {
        print_error();
        exit(1);
    }

    FILE *input = stdin;
    int interactive = 1;
    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (!input) {
            print_error();
            exit(1);
        }
        interactive = 0;
    }

    path_list p;
    init_path(&p);

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while (1) {
        if (interactive) {
            // imprimir el prompt 
            if (write(STDOUT_FILENO, "wish> ", 6) < 0) {
                // si falla el write del prompt, todavía intentamos leer
            }
        }

        read = getline(&line, &len, input);
        if (read == -1) {
            // si se ejecuta un error, salimos del modo interactivo
            if (input != stdin) fclose(input);
            free(line);
            // y se libera la memoria del path list
            for (int i=0; i<p.count; ++i) free(p.dirs[i]);
            exit(0);
        }

        // quitar '\n' al final de la linea
        if (read > 0 && line[read-1] == '\n') line[read-1] = '\0';

        // ignorar lineas en blanco
        char *tline = trim(line);
        if (tline == NULL || strlen(tline) == 0) {
            continue;
        }

        // procesar la linea leida
        process_line(tline, &p);
    }

    return 0;
}
