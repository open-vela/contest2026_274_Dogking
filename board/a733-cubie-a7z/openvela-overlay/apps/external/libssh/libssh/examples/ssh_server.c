/* This is a sample implementation of a libssh based SSH server */
/*
Copyright 2014 Audrius Butkevicius

This file is part of the SSH Library

You are free to copy this file, modify it in any way, consider it being public
domain. This does not apply to the rest of the library though, but it is
allowed to cut-and-paste working code from this file to any license of
program.
The goal is to show the API in action.
*/

#include "config.h"

#include <libssh/callbacks.h>
#include <libssh/server.h>

#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#include <errno.h>
#include <poll.h>
#ifdef HAVE_ARGP_H
#include <argp.h>
#endif
#include <fcntl.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#include <pthread.h>
#ifdef HAVE_PTY_H
#include <pty.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <spawn.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef CONFIG_SYSTEM_A733SERVICES
#include <system/a733_services.h>
#endif

#ifndef BUF_SIZE
/* Keep channel forwarding within the small NuttX worker-thread stack. */
#define BUF_SIZE 4096
#endif

#ifndef KEYS_FOLDER
#ifdef _WIN32
#define KEYS_FOLDER
#else
#define KEYS_FOLDER "/etc/ssh/"
#endif
#endif

#define SESSION_END (SSH_CLOSED | SSH_CLOSED_ERROR)
#define SFTP_SERVER_PATH "/usr/lib/sftp-server"

/* NuttX built-ins may leave a listening TCP endpoint behind when the default
 * SIGINT action terminates sshd inside an NSH task group.  In particular, a
 * Windows Test-NetConnection followed by Ctrl+C can make the next bind fail
 * with EADDRINUSE even though no sshd task is visible in ps.  Catch the stop
 * signals so accept(2) returns EINTR and the normal ssh_bind_free() path gets
 * a chance to close the listening socket.
 */

static volatile sig_atomic_t g_sshd_stop_requested;

/* Flat-build globals are shared by every invocation and session.  A second
 * launch must not erase the credentials of a running server before bind(). */
static pthread_mutex_t g_server_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_sessions_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_sessions_done = PTHREAD_COND_INITIALIZER;
#define SSHD_MAX_SESSIONS 4
static ssh_session g_sessions[SSHD_MAX_SESSIONS];
static pid_t g_server_pid;
static int g_server_listening;

static void sshd_stop_handler(int signo)
{
    (void)signo;
    g_sshd_stop_requested = 1;
}

static void set_default_keys(ssh_bind sshbind,
                             int rsa_already_set,
                             int dsa_already_set,
                             int ecdsa_already_set) {
    if (!rsa_already_set) {
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY,
                             KEYS_FOLDER "ssh_host_rsa_key");
    }
    if (!dsa_already_set) {
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_DSAKEY,
                             KEYS_FOLDER "ssh_host_dsa_key");
    }
    if (!ecdsa_already_set) {
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_ECDSAKEY,
                             KEYS_FOLDER "ssh_host_ecdsa_key");
    }
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY,
                         KEYS_FOLDER "ssh_host_ed25519_key");
}
#define DEF_STR_SIZE 1024
char authorizedkeys[DEF_STR_SIZE] = {0};
/* Never ship a usable example credential.  The NuttX sshd must be started
 * with -u/-P, or with -a for public-key authentication. */

char username[128] = {0};
char password[128] = {0};
char hostkey_path[DEF_STR_SIZE] = {0};

#define SSHD_AUTH_FILE "/data/ssh/sshd.auth"
#define SSHD_HOSTKEY_FILE "/data/ssh/ssh_host_ed25519_key"
#define SSHD_AUTHORIZED_KEYS_FILE "/data/ssh/authorized_keys"
#define SSHD_AUTH_ITERATIONS 120000
#define SSHD_AUTH_SALT_SIZE 16
#define SSHD_AUTH_HASH_SIZE 32

static unsigned char password_salt[SSHD_AUTH_SALT_SIZE];
static unsigned char password_hash[SSHD_AUTH_HASH_SIZE];
static unsigned int password_iterations;
static int password_hash_loaded;

static int hex_value(int ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int hex_decode(const char *text, unsigned char *out, size_t outlen)
{
    size_t i;

    if (strlen(text) != outlen * 2) {
        return -1;
    }

    for (i = 0; i < outlen; i++) {
        int hi = hex_value(text[i * 2]);
        int lo = hex_value(text[i * 2 + 1]);

        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

static void hex_encode(const unsigned char *in, size_t inlen, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < inlen; i++) {
        out[i * 2] = digits[in[i] >> 4];
        out[i * 2 + 1] = digits[in[i] & 15];
    }
    out[inlen * 2] = '\0';
}

static int derive_password(const char *pass,
                           const unsigned char *salt,
                           unsigned int iterations,
                           unsigned char *out)
{
    const mbedtls_md_info_t *info;
    mbedtls_md_context_t ctx;
    int rc;

    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return -1;
    }

    mbedtls_md_init(&ctx);
    rc = mbedtls_md_setup(&ctx, info, 1);
    if (rc == 0) {
        rc = mbedtls_pkcs5_pbkdf2_hmac(&ctx,
                                      (const unsigned char *)pass,
                                      strlen(pass), salt,
                                      SSHD_AUTH_SALT_SIZE, iterations,
                                      SSHD_AUTH_HASH_SIZE, out);
    }
    mbedtls_md_free(&ctx);
    return rc;
}

static int load_auth_file(const char *path)
{
    FILE *stream;
    char line[320];
    char salt_hex[SSHD_AUTH_SALT_SIZE * 2 + 1];
    char hash_hex[SSHD_AUTH_HASH_SIZE * 2 + 1];
    unsigned int iterations;

    stream = fopen(path, "r");
    if (stream == NULL) {
        return -1;
    }

    if (fgets(line, sizeof(line), stream) == NULL ||
        sscanf(line, "v1 %127s %u %32s %64s", username, &iterations,
               salt_hex, hash_hex) != 4 ||
        iterations < 10000 || iterations > 1000000 ||
        hex_decode(salt_hex, password_salt, sizeof(password_salt)) < 0 ||
        hex_decode(hash_hex, password_hash, sizeof(password_hash)) < 0) {
        fclose(stream);
        fprintf(stderr, "Invalid SSH authentication file: %s\n", path);
        return -1;
    }

    fclose(stream);
    password_iterations = iterations;
    password_hash_loaded = 1;
    return 0;
}

static int setup_auth_file(const char *user)
{
    unsigned char salt[SSHD_AUTH_SALT_SIZE];
    unsigned char hash[SSHD_AUTH_HASH_SIZE];
    char salt_hex[SSHD_AUTH_SALT_SIZE * 2 + 1];
    char hash_hex[SSHD_AUTH_HASH_SIZE * 2 + 1];
    char first[128] = {0};
    char second[128] = {0};
    char temporary[DEF_STR_SIZE];
    FILE *random;
    FILE *stream;
    int rc = 1;

    if (user == NULL || user[0] == '\0' || strlen(user) >= sizeof(username) ||
        strpbrk(user, " \t\r\n") != NULL) {
        fprintf(stderr, "Username must be a non-empty single word\n");
        return 1;
    }

    if (ssh_getpass("New SSH password: ", first, sizeof(first), 0, 0) < 0 ||
        ssh_getpass("Verify SSH password: ", second, sizeof(second), 0, 0) < 0) {
        fprintf(stderr, "Unable to read SSH password\n");
        goto out;
    }

    if (strlen(first) < 8 || strcmp(first, second) != 0) {
        fprintf(stderr, "Passwords differ or contain fewer than 8 characters\n");
        goto out;
    }

    random = fopen("/dev/urandom", "rb");
    if (random == NULL || fread(salt, 1, sizeof(salt), random) != sizeof(salt)) {
        if (random != NULL) {
            fclose(random);
        }
        fprintf(stderr, "Unable to obtain secure random salt\n");
        goto out;
    }
    fclose(random);

    if (derive_password(first, salt, SSHD_AUTH_ITERATIONS, hash) != 0) {
        fprintf(stderr, "Unable to derive SSH password hash\n");
        goto out;
    }

    mkdir("/data/ssh", S_IRWXU);
    snprintf(temporary, sizeof(temporary), "%s.tmp", SSHD_AUTH_FILE);
    stream = fopen(temporary, "w");
    if (stream == NULL) {
        fprintf(stderr, "Unable to create %s: %s\n", temporary,
                strerror(errno));
        goto out;
    }

    hex_encode(salt, sizeof(salt), salt_hex);
    hex_encode(hash, sizeof(hash), hash_hex);
    fprintf(stream, "v1 %s %u %s %s\n", user, SSHD_AUTH_ITERATIONS,
            salt_hex, hash_hex);
    if (fflush(stream) != 0 || fsync(fileno(stream)) != 0) {
        fclose(stream);
        unlink(temporary);
        fprintf(stderr, "Unable to flush SSH authentication file\n");
        goto out;
    }
    if (fclose(stream) != 0 || rename(temporary, SSHD_AUTH_FILE) != 0) {
        unlink(temporary);
        fprintf(stderr, "Unable to commit %s: %s\n", SSHD_AUTH_FILE,
                strerror(errno));
        goto out;
    }

    chmod(SSHD_AUTH_FILE, S_IRUSR | S_IWUSR);
    printf("SSH authentication configured for '%s'. Reboot or run 'sshd'.\n",
           user);
    rc = 0;

out:
    explicit_bzero(first, sizeof(first));
    explicit_bzero(second, sizeof(second));
    explicit_bzero(hash, sizeof(hash));
    return rc;
}

static int ensure_host_key(ssh_bind sshbind)
{
    struct stat info;
    ssh_key key = NULL;
    int rc;

    if (hostkey_path[0] == '\0') {
        return SSH_OK;
    }

    if (stat(hostkey_path, &info) < 0) {
        if (errno != ENOENT) {
            fprintf(stderr, "Cannot inspect host key %s: %s\n",
                    hostkey_path, strerror(errno));
            return SSH_ERROR;
        }

        rc = ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key);
        if (rc == SSH_OK) {
            rc = ssh_pki_export_privkey_file(key, NULL, NULL, NULL,
                                             hostkey_path);
        }

        ssh_key_free(key);
        if (rc != SSH_OK) {
            fprintf(stderr, "Failed to generate host key %s\n", hostkey_path);
            return SSH_ERROR;
        }

        chmod(hostkey_path, S_IRUSR | S_IWUSR);
        printf("Generated board-local ED25519 host key: %s\n", hostkey_path);
    }

    /* HOSTKEY imports the private key immediately.  The -k option may name
     * a file which does not exist until the block above creates it. */

    rc = ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY,
                              hostkey_path);
    if (rc != SSH_OK) {
        fprintf(stderr, "Cannot bind host key %s: %s\n",
                hostkey_path, ssh_get_error(sshbind));
        return SSH_ERROR;
    }

    return SSH_OK;
}
#ifdef HAVE_ARGP_H
const char *argp_program_version = "libssh server example "
SSH_STRINGIFY(LIBSSH_VERSION);
const char *argp_program_bug_address = "<libssh@libssh.org>";

/* Program documentation. */
static char doc[] = "libssh -- a Secure Shell protocol implementation";

/* A description of the arguments we accept. */
static char args_doc[] = "BINDADDR";

/* The options we understand. */
static struct argp_option options[] = {
    {
        .name  = "port",
        .key   = 'p',
        .arg   = "PORT",
        .flags = 0,
        .doc   = "Set the port to bind.",
        .group = 0
    },
    {
        .name  = "hostkey",
        .key   = 'k',
        .arg   = "FILE",
        .flags = 0,
        .doc   = "Set a host key.  Can be used multiple times.  "
                 "Implies no default keys.",
        .group = 0
    },
    {
        .name  = "dsakey",
        .key   = 'd',
        .arg   = "FILE",
        .flags = 0,
        .doc   = "Set the dsa key.",
        .group = 0
    },
    {
        .name  = "rsakey",
        .key   = 'r',
        .arg   = "FILE",
        .flags = 0,
        .doc   = "Set the rsa key.",
        .group = 0
    },
    {
        .name  = "ecdsakey",
        .key   = 'e',
        .arg   = "FILE",
        .flags = 0,
        .doc   = "Set the ecdsa key.",
        .group = 0
    },
    {
        .name  = "authorizedkeys",
        .key   = 'a',
        .arg   = "FILE",
        .flags = 0,
        .doc   = "Set the authorized keys file.",
        .group = 0
    },
    {
        .name  = "user",
        .key   = 'u',
        .arg   = "USERNAME",
        .flags = 0,
        .doc   = "Set expected username.",
        .group = 0
    },
    {
        .name  = "pass",
        .key   = 'P',
        .arg   = "PASSWORD",
        .flags = 0,
        .doc   = "Set expected password.",
        .group = 0
    },
    {
        .name  = "no-default-keys",
        .key   = 'n',
        .arg   = NULL,
        .flags = 0,
        .doc   = "Do not set default key locations.",
        .group = 0
    },
    {
        .name  = "verbose",
        .key   = 'v',
        .arg   = NULL,
        .flags = 0,
        .doc   = "Get verbose output.",
        .group = 0
    },
    {NULL, 0, NULL, 0, NULL, 0}
};

/* Parse a single option. */
static error_t parse_opt (int key, char *arg, struct argp_state *state) {
    /* Get the input argument from argp_parse, which we
     * know is a pointer to our arguments structure. */
    ssh_bind sshbind = state->input;
    static int no_default_keys = 0;
    static int rsa_already_set = 0, dsa_already_set = 0, ecdsa_already_set = 0;

    switch (key) {
        case 'n':
            no_default_keys = 1;
            break;
        case 'p':
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, arg);
            break;
        case 'd':
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_DSAKEY, arg);
            dsa_already_set = 1;
            break;
        case 'k':
            strncpy(hostkey_path, arg, sizeof(hostkey_path) - 1);
            /* We can't track the types of keys being added with this
               option, so let's ensure we keep the keys we're adding
               by just not setting the default keys */
            no_default_keys = 1;
            break;
        case 'r':
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, arg);
            rsa_already_set = 1;
            break;
        case 'e':
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_ECDSAKEY, arg);
            ecdsa_already_set = 1;
            break;
        case 'a':
            strncpy(authorizedkeys, arg, DEF_STR_SIZE-1);
            break;
        case 'u':
            strncpy(username, arg, sizeof(username) - 1);
            break;
        case 'P':
            strncpy(password, arg, sizeof(password) - 1);
            break;
        case 'v':
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_LOG_VERBOSITY_STR,
                                 "3");
            break;
        case ARGP_KEY_ARG:
            if (state->arg_num >= 1) {
                /* Too many arguments. */
                argp_usage (state);
            }
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, arg);
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 1) {
                /* Not enough arguments. */
                argp_usage (state);
            }

            if (!no_default_keys) {
                set_default_keys(sshbind,
                                 rsa_already_set,
                                 dsa_already_set,
                                 ecdsa_already_set);
            }

            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/* Our argp parser. */
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};
#else
static int parse_opt(int argc, char **argv, ssh_bind sshbind) {
    int no_default_keys = 0;
    int rsa_already_set = 0;
    int dsa_already_set = 0;
    int ecdsa_already_set = 0;
    int key;

    while((key = getopt(argc, argv, "a:d:e:k:np:P:r:u:v")) != -1) {
        if (key == 'n') {
            no_default_keys = 1;
        } else if (key == 'p') {
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, optarg);
        } else if (key == 'd') {
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_DSAKEY, optarg);
            dsa_already_set = 1;
        } else if (key == 'k') {
            strncpy(hostkey_path, optarg, sizeof(hostkey_path) - 1);
            /* We can't track the types of keys being added with this
            option, so let's ensure we keep the keys we're adding
            by just not setting the default keys */
            no_default_keys = 1;
        } else if (key == 'r') {
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, optarg);
            rsa_already_set = 1;
        } else if (key == 'e') {
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_ECDSAKEY, optarg);
            ecdsa_already_set = 1;
        } else if (key == 'a') {
            strncpy(authorizedkeys, optarg, DEF_STR_SIZE-1);
        } else if (key == 'u') {
            strncpy(username, optarg, sizeof(username) - 1);
        } else if (key == 'P') {
            strncpy(password, optarg, sizeof(password) - 1);
        } else if (key == 'v') {
            ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_LOG_VERBOSITY_STR,
                                 "3");
        } else {
            break;
        }
    }

    if (key != -1) {
        printf("Usage: %s [OPTION...] BINDADDR\n"
               "libssh %s -- a Secure Shell protocol implementation\n"
               "\n"
               "  -a, --authorizedkeys=FILE  Set the authorized keys file.\n"
               "  -d, --dsakey=FILE          Set the dsa key.\n"
               "  -e, --ecdsakey=FILE        Set the ecdsa key.\n"
               "  -k, --hostkey=FILE         Set a host key.  Can be used multiple times.\n"
               "                             Implies no default keys.\n"
               "  -n, --no-default-keys      Do not set default key locations.\n"
               "  -p, --port=PORT            Set the port to bind.\n"
               "  -P, --pass=PASSWORD        Set expected password.\n"
               "  -r, --rsakey=FILE          Set the rsa key.\n"
               "  -u, --user=USERNAME        Set expected username.\n"
               "  -v, --verbose              Get verbose output.\n"
               "  -?, --help                 Give this help list\n"
               "\n"
               "Mandatory or optional arguments to long options are also mandatory or optional\n"
               "for any corresponding short options.\n"
               "\n"
               "Report bugs to <libssh@libssh.org>.\n",
               argv[0], SSH_STRINGIFY(LIBSSH_VERSION));
        return -1;
    }

    if (optind != argc - 1) {
        printf("Usage: %s [OPTION...] BINDADDR\n", argv[0]);
        return -1;
    }

    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, argv[optind]);

    if (!no_default_keys) {
        set_default_keys(sshbind,
                         rsa_already_set,
                         dsa_already_set,
                         ecdsa_already_set);
    }

    return 0;
}
#endif /* HAVE_ARGP_H */

/* A userdata struct for channel. */
struct channel_data_struct {
    /* pid of the child process the channel will spawn. */
    pid_t pid;
    /* For PTY allocation */
    socket_t pty_master;
    socket_t pty_slave;
    /* For communication with the child process. */
    socket_t child_stdin;
    socket_t child_stdout;
    /* Only used for subsystem and exec requests. */
    socket_t child_stderr;
    /* Event which is used to poll the above descriptors. */
    ssh_event event;
    /* Terminal size struct. */
    struct winsize *winsize;
};

/* A userdata struct for session. */
struct session_data_struct {
    /* Pointer to the channel the session will allocate. */
    ssh_channel channel;
    int auth_attempts;
    int authenticated;
};

static int data_function(ssh_session session, ssh_channel channel, void *data,
                         uint32_t len, int is_stderr, void *userdata) {
    struct channel_data_struct *cdata = (struct channel_data_struct *) userdata;

    (void) session;
    (void) channel;
    (void) is_stderr;

    if (len == 0 || cdata->pid < 1 || kill(cdata->pid, 0) < 0) {
        return 0;
    }

    return write(cdata->child_stdin, (char *) data, len);
}

static int pty_request(ssh_session session, ssh_channel channel,
                       const char *term, int cols, int rows, int py, int px,
                       void *userdata) {
    struct channel_data_struct *cdata = (struct channel_data_struct *)userdata;

    (void) session;
    (void) channel;
    (void) term;

    cdata->winsize->ws_row = rows;
    cdata->winsize->ws_col = cols;
    cdata->winsize->ws_xpixel = px;
    cdata->winsize->ws_ypixel = py;

    if (openpty(&cdata->pty_master, &cdata->pty_slave, NULL, NULL,
                cdata->winsize) != 0) {
        fprintf(stderr, "Failed to open pty\n");
        return SSH_ERROR;
    }

    /* Make pty_slave become the controlling terminal */
#ifdef TIOCSCTTY
    ioctl(cdata->pty_slave, TIOCSCTTY, (unsigned long)0);
#endif

    return SSH_OK;
}

static int pty_resize(ssh_session session, ssh_channel channel, int cols,
                      int rows, int py, int px, void *userdata) {
    struct channel_data_struct *cdata = (struct channel_data_struct *)userdata;

    (void) session;
    (void) channel;

    cdata->winsize->ws_row = rows;
    cdata->winsize->ws_col = cols;
    cdata->winsize->ws_xpixel = px;
    cdata->winsize->ws_ypixel = py;

    if (cdata->pty_master != -1) {
        return ioctl(cdata->pty_master, TIOCSWINSZ, cdata->winsize);
    }

    return SSH_ERROR;
}

static int exec_pty(const char *mode, const char *command,
                    struct channel_data_struct *cdata) {
    const char *args[] = {"sh", NULL, NULL, NULL};
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attr;
    int rc;

    /* Linux uses `sh -l` for an interactive login shell.  NuttX NSH does not
     * implement -l and reports `nsh: sh: syntax error`, immediately closing a
     * fully authenticated SSH connection.  Keep -c for explicit exec
     * requests, but launch an interactive NSH as plain `sh`.
     */

    if (mode != NULL) {
        args[1] = mode;
        args[2] = command;
    }

    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, cdata->pty_slave, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, cdata->pty_slave, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, cdata->pty_slave, STDERR_FILENO);

    posix_spawnattr_init(&attr);
#ifdef POSIX_SPAWN_SETSID
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
#endif

    rc = posix_spawn(&cdata->pid, args[0], &file_actions, &attr,
                     (char *const *)args, NULL);
    if (rc != 0) {
        fprintf(stderr, "Unable to start NSH on SSH PTY: %s\n", strerror(rc));
        posix_spawn_file_actions_destroy(&file_actions);
        posix_spawnattr_destroy(&attr);
        return SSH_ERROR;
    }

    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&attr);
    close(cdata->pty_slave);
    cdata->pty_slave = -1;
    /* pty fd is bi-directional */
    cdata->child_stdout = cdata->child_stdin = cdata->pty_master;
    return SSH_OK;
}

static int exec_nopty(const char *command, struct channel_data_struct *cdata) {
    const char *interactive_args[] = {"sh", NULL};
    const char *command_args[] = {"sh", "-c", command, NULL};
    const char **args = command != NULL ? command_args : interactive_args;
    posix_spawn_file_actions_t file_actions;
    int in[2], out[2], err[2];
    int rc;

    /* Do the plumbing to be able to talk with the child process. */
    if (pipe(in) != 0) {
        goto stdin_failed;
    }
    if (pipe(out) != 0) {
        goto stdout_failed;
    }
    if (pipe(err) != 0) {
        goto stderr_failed;
    }

    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, err[1], STDERR_FILENO);
    rc = posix_spawn(&cdata->pid, args[0], &file_actions, NULL,
                     (char *const *)args, NULL);
    if (rc != 0) {
        fprintf(stderr, "Unable to start NSH for SSH channel: %s\n", strerror(rc));
        goto spawn_failed;
    }

    close(in[0]);
    close(out[1]);
    close(err[1]);

    cdata->child_stdin = in[1];
    cdata->child_stdout = out[0];
    cdata->child_stderr = err[0];

    return SSH_OK;

spawn_failed:
    posix_spawn_file_actions_destroy(&file_actions);
    close(err[0]);
    close(err[1]);
stderr_failed:
    close(out[0]);
    close(out[1]);
stdout_failed:
    close(in[0]);
    close(in[1]);
stdin_failed:
    return SSH_ERROR;
}

static int exec_request(ssh_session session, ssh_channel channel,
                        const char *command, void *userdata) {
    struct channel_data_struct *cdata = (struct channel_data_struct *) userdata;


    (void) session;
    (void) channel;

    if(cdata->pid > 0) {
        return SSH_ERROR;
    }

    if (cdata->pty_master != -1 && cdata->pty_slave != -1) {
        return exec_pty("-c", command, cdata);
    }
    return exec_nopty(command, cdata);
}

static int shell_request(ssh_session session, ssh_channel channel,
                         void *userdata) {
    struct channel_data_struct *cdata = (struct channel_data_struct *) userdata;

    (void) session;
    (void) channel;

    if(cdata->pid > 0) {
        return SSH_ERROR;
    }

    if (cdata->pty_master != -1 && cdata->pty_slave != -1) {
        return exec_pty(NULL, NULL, cdata);
    }
    return exec_nopty(NULL, cdata);
}

static int subsystem_request(ssh_session session, ssh_channel channel,
                             const char *subsystem, void *userdata) {
    /* subsystem requests behave simillarly to exec requests. */
    if (strcmp(subsystem, "sftp") == 0) {
        return exec_request(session, channel, SFTP_SERVER_PATH, userdata);
    }
    return SSH_ERROR;
}

static int auth_password(ssh_session session, const char *user,
                         const char *pass, void *userdata) {
    struct session_data_struct *sdata = (struct session_data_struct *) userdata;
    unsigned char candidate[SSHD_AUTH_HASH_SIZE];
    unsigned char different = 0;
    size_t i;

    (void) session;

    if (password_hash_loaded && strcmp(user, username) == 0 &&
        derive_password(pass, password_salt, password_iterations,
                        candidate) == 0) {
        for (i = 0; i < sizeof(candidate); i++) {
            different |= candidate[i] ^ password_hash[i];
        }
        explicit_bzero(candidate, sizeof(candidate));
        if (different == 0) {
            sdata->authenticated = 1;
            return SSH_AUTH_SUCCESS;
        }
    } else if (username[0] != '\0' && password[0] != '\0' &&
               strcmp(user, username) == 0 && strcmp(pass, password) == 0) {
        sdata->authenticated = 1;
        return SSH_AUTH_SUCCESS;
    }

    sdata->auth_attempts++;
    return SSH_AUTH_DENIED;
}

static int auth_publickey(ssh_session session,
                          const char *user,
                          struct ssh_key_struct *pubkey,
                          char signature_state,
                          void *userdata)
{
    struct session_data_struct *sdata = (struct session_data_struct *) userdata;

    (void) user;
    (void) session;

    if (signature_state == SSH_PUBLICKEY_STATE_NONE) {
        return SSH_AUTH_SUCCESS;
    }

    if (signature_state != SSH_PUBLICKEY_STATE_VALID) {
        return SSH_AUTH_DENIED;
    }

    // valid so far.  Now look through authorized keys for a match
    if (authorizedkeys[0]) {
        ssh_key key = NULL;
        int result;
        struct stat buf;

        if (stat(authorizedkeys, &buf) == 0) {
            result = ssh_pki_import_pubkey_file( authorizedkeys, &key );
            if ((result != SSH_OK) || (key==NULL)) {
                fprintf(stderr,
                        "Unable to import public key file %s\n",
                        authorizedkeys);
            } else {
                result = ssh_key_cmp( key, pubkey, SSH_KEY_CMP_PUBLIC );
                ssh_key_free(key);
                if (result == 0) {
                    sdata->authenticated = 1;
                    return SSH_AUTH_SUCCESS;
                }
            }
        }
    }

    // no matches
    sdata->authenticated = 0;
    return SSH_AUTH_DENIED;
}

static ssh_channel channel_open(ssh_session session, void *userdata) {
    struct session_data_struct *sdata = (struct session_data_struct *) userdata;

    sdata->channel = ssh_channel_new(session);
    return sdata->channel;
}

static int process_stdout(socket_t fd, int revents, void *userdata) {
    char buf[BUF_SIZE];
    int n = -1;
    ssh_channel channel = (ssh_channel) userdata;

    if (channel != NULL && (revents & POLLIN) != 0) {
        n = read(fd, buf, BUF_SIZE);
        if (n > 0) {
            ssh_channel_write(channel, buf, n);
        }
    }

    return n;
}

static int process_stderr(socket_t fd, int revents, void *userdata) {
    char buf[BUF_SIZE];
    int n = -1;
    ssh_channel channel = (ssh_channel) userdata;

    if (channel != NULL && (revents & POLLIN) != 0) {
        n = read(fd, buf, BUF_SIZE);
        if (n > 0) {
            ssh_channel_write_stderr(channel, buf, n);
        }
    }

    return n;
}

static void handle_session(ssh_event event, ssh_session session) {
    int n;
    int rc = 0;

    /* Structure for storing the pty size. */
    struct winsize wsize = {
        .ws_row = 0,
        .ws_col = 0,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    /* Our struct holding information about the channel. */
    struct channel_data_struct cdata = {
        .pid = 0,
        .pty_master = -1,
        .pty_slave = -1,
        .child_stdin = -1,
        .child_stdout = -1,
        .child_stderr = -1,
        .event = NULL,
        .winsize = &wsize
    };

    /* Our struct holding information about the session. */
    struct session_data_struct sdata = {
        .channel = NULL,
        .auth_attempts = 0,
        .authenticated = 0
    };

    struct ssh_channel_callbacks_struct channel_cb = {
        .userdata = &cdata,
        .channel_pty_request_function = pty_request,
        .channel_pty_window_change_function = pty_resize,
        .channel_shell_request_function = shell_request,
        .channel_exec_request_function = exec_request,
        .channel_data_function = data_function,
        .channel_subsystem_request_function = subsystem_request
    };

    struct ssh_server_callbacks_struct server_cb = {
        .userdata = &sdata,
        .auth_password_function = auth_password,
        .channel_open_request_session_function = channel_open,
    };

    if (authorizedkeys[0]) {
        server_cb.auth_pubkey_function = auth_publickey;
        ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY);
    } else
        ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD);

    ssh_callbacks_init(&server_cb);
    ssh_callbacks_init(&channel_cb);

    ssh_set_server_callbacks(session, &server_cb);

    if (ssh_handle_key_exchange(session) != SSH_OK) {
        fprintf(stderr, "%s\n", ssh_get_error(session));
        return;
    }

    ssh_event_add_session(event, session);

    n = 0;
    while (sdata.authenticated == 0 || sdata.channel == NULL) {
        /* If the user has used up all attempts, or if he hasn't been able to
         * authenticate in 10 seconds (n * 100ms), disconnect. */
        if (sdata.auth_attempts >= 3 || n >= 100) {
            return;
        }

        if (ssh_event_dopoll(event, 100) == SSH_ERROR) {
            fprintf(stderr, "%s\n", ssh_get_error(session));
            return;
        }
        n++;
    }

    ssh_set_channel_callbacks(sdata.channel, &channel_cb);

    do {
        /* Poll the main event which takes care of the session, the channel and
         * even our child process's stdout/stderr (once it's started). */
        if (ssh_event_dopoll(event, -1) == SSH_ERROR) {
          ssh_channel_close(sdata.channel);
        }

        /* If child process's stdout/stderr has been registered with the event,
         * or the child process hasn't started yet, continue. */
        if (cdata.event != NULL || cdata.pid == 0) {
            continue;
        }
        /* Executed only once, once the child process starts. */
        cdata.event = event;
        /* If stdout valid, add stdout to be monitored by the poll event. */
        if (cdata.child_stdout != -1) {
            if (ssh_event_add_fd(event, cdata.child_stdout, POLLIN, process_stdout,
                                 sdata.channel) != SSH_OK) {
                fprintf(stderr, "Failed to register stdout to poll context\n");
                ssh_channel_close(sdata.channel);
            }
        }

        /* If stderr valid, add stderr to be monitored by the poll event. */
        if (cdata.child_stderr != -1){
            if (ssh_event_add_fd(event, cdata.child_stderr, POLLIN, process_stderr,
                                 sdata.channel) != SSH_OK) {
                fprintf(stderr, "Failed to register stderr to poll context\n");
                ssh_channel_close(sdata.channel);
            }
        }
    } while(ssh_channel_is_open(sdata.channel) &&
            (cdata.pid == 0 || waitpid(cdata.pid, &rc, WNOHANG) == 0));

    if (cdata.pty_slave != -1) {
        close(cdata.pty_slave);
    }
    if (cdata.pty_master != -1) {
        close(cdata.pty_master);
    } else {
        close(cdata.child_stdin);
        close(cdata.child_stdout);
        close(cdata.child_stderr);
    }

    /* Remove the descriptors from the polling context, since they are now
     * closed, they will always trigger during the poll calls. */
    ssh_event_remove_fd(event, cdata.child_stdout);
    ssh_event_remove_fd(event, cdata.child_stderr);

    /* If the child process exited. */
    if (kill(cdata.pid, 0) < 0 && WIFEXITED(rc)) {
        rc = WEXITSTATUS(rc);
        ssh_channel_request_send_exit_status(sdata.channel, rc);
    /* If client terminated the channel or the process did not exit nicely,
     * but only if something has been forked. */
    } else if (cdata.pid > 0) {
        kill(cdata.pid, SIGKILL);
    }

    ssh_channel_send_eof(sdata.channel);
    ssh_channel_close(sdata.channel);

    /* Wait up to 5 seconds for the client to terminate the session. */
    for (n = 0; n < 50 && (ssh_get_status(session) & SESSION_END) == 0; n++) {
        ssh_event_dopoll(event, 100);
    }
}

#ifdef WITH_FORK
/* SIGCHLD handler for cleaning up dead children. */
static void sigchld_handler(int signo) {
    (void) signo;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}
#else
static void *session_thread(void *arg) {
    ssh_session session = arg;
    ssh_event event;

    event = ssh_event_new();
    if (event != NULL) {
        /* Blocks until the SSH session ends by either
         * child thread exiting, or client disconnecting. */
        handle_session(event, session);
        ssh_event_free(event);
    } else {
        fprintf(stderr, "Could not create polling context\n");
    }
    ssh_disconnect(session);
    pthread_mutex_lock(&g_sessions_lock);
    for (int i = 0; i < SSHD_MAX_SESSIONS; i++) {
        if (g_sessions[i] == session) {
            g_sessions[i] = NULL;
            break;
        }
    }
    ssh_free(session);
    pthread_cond_broadcast(&g_sessions_done);
    pthread_mutex_unlock(&g_sessions_lock);
    return NULL;
}
#endif

static int sshd_run(int argc, char **argv) {
    ssh_bind sshbind;
    ssh_session session;
    struct sigaction stop_action;
    struct sigaction old_sigint;
    struct sigaction old_sigterm;
    int sigint_installed = 0;
    int sigterm_installed = 0;
    int rc;
    int service_mode = argc == 1 ||
                       (argc == 2 && strcmp(argv[1], "--service") == 0);
    struct stat info;
#ifdef WITH_FORK
    struct sigaction sa;

    /* Set up SIGCHLD handler. */
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) != 0) {
        fprintf(stderr, "Failed to register SIGCHLD handler\n");
        return 1;
    }
#endif

    if (argc == 3 && strcmp(argv[1], "--setup") == 0) {
        return setup_auth_file(argv[2]);
    }

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "-?") == 0)) {
        printf("Usage: %s [options] BINDADDR\n"
               "  -k FILE  host private key (recommended)\n"
               "  -a FILE  authorized_keys file\n"
               "  -p PORT  listening port\n"
               "  -u USER  password-auth username\n"
               "  -P PASS  password (testing only; visible in history)\n"
               "  -n       do not use default host-key paths\n",
               argv[0]);
        printf("\nService mode (recommended):\n"
               "  %s --setup USER   securely configure persistent login\n"
               "  %s                listen on 0.0.0.0:22 using /data/ssh\n",
               argv[0], argv[0]);
        printf("  %s --status       show real listener/session state\n",
               argv[0]);
        return 0;
    }

    memset(authorizedkeys, 0, sizeof(authorizedkeys));
    memset(username, 0, sizeof(username));
    explicit_bzero(password, sizeof(password));
    memset(hostkey_path, 0, sizeof(hostkey_path));
    memset(password_salt, 0, sizeof(password_salt));
    memset(password_hash, 0, sizeof(password_hash));
    password_iterations = 0;
    password_hash_loaded = 0;
    optind = 1;

    rc = ssh_init();
    if (rc < 0) {
        syslog(LOG_ERR, "SSHD: ssh_init failed\n");
        fprintf(stderr, "ssh_init failed\n");
        return 1;
    }

    sshbind = ssh_bind_new();
    if (sshbind == NULL) {
        syslog(LOG_ERR, "SSHD: bind allocation failed\n");
        fprintf(stderr, "ssh_bind_new failed\n");
        ssh_finalize();
        return 1;
    }

    if (service_mode) {
        mkdir("/data/ssh", S_IRWXU);
        strncpy(hostkey_path, SSHD_HOSTKEY_FILE, sizeof(hostkey_path) - 1);
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, "0.0.0.0");
        ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, "22");
        if (stat(SSHD_AUTHORIZED_KEYS_FILE, &info) == 0) {
            strncpy(authorizedkeys, SSHD_AUTHORIZED_KEYS_FILE,
                    sizeof(authorizedkeys) - 1);
        }
        if (load_auth_file(SSHD_AUTH_FILE) < 0 && authorizedkeys[0] == '\0') {
            syslog(LOG_ERR, "SSHD: authentication unavailable; run --setup locally\n");
            ssh_bind_free(sshbind);
            ssh_finalize();
            return 1;
        }
        if (username[0] == '\0') {
            snprintf(username, sizeof(username), "openvela");
        }
    } else {
#ifdef HAVE_ARGP_H
        argp_parse(&argp, argc, argv, 0, 0, sshbind);
#else
        if (parse_opt(argc, argv, sshbind) < 0) {
            ssh_bind_free(sshbind);
            ssh_finalize();
            return 1;
        }
#endif /* HAVE_ARGP_H */
    }

    if (ensure_host_key(sshbind) != SSH_OK) {
        syslog(LOG_ERR, "SSHD: host key unavailable: %s\n", hostkey_path);
        ssh_bind_free(sshbind);
        ssh_finalize();
        return 1;
    }

    if (!service_mode && !password_hash_loaded &&
        username[0] != '\0' && password[0] == '\0') {
        if (ssh_getpass("SSH password: ", password, sizeof(password), 0, 1) < 0) {
            fprintf(stderr, "Unable to read SSH password\n");
            ssh_bind_free(sshbind);
            ssh_finalize();
            return 1;
        }
    }

    if (authorizedkeys[0] == '\0' &&
        !password_hash_loaded &&
        (username[0] == '\0' || password[0] == '\0')) {
        fprintf(stderr,
                "Refusing to start without authentication. Run "
                "'sshd --setup openvela' once, or use -a/-u.\n");
        ssh_bind_free(sshbind);
        ssh_finalize();
        return 1;
    }

    if(ssh_bind_listen(sshbind) < 0) {
        syslog(LOG_ERR, "SSHD: listen failed: %s\n", ssh_get_error(sshbind));
        fprintf(stderr, "%s\n", ssh_get_error(sshbind));
        ssh_bind_free(sshbind);
        ssh_finalize();
        return 1;
    }

    pthread_mutex_lock(&g_sessions_lock);
    g_server_listening = 1;
    pthread_mutex_unlock(&g_sessions_lock);
    syslog(LOG_INFO, "SSHD: listening pid=%d %s; max-sessions=%d\n",
           (int)getpid(), service_mode ? "0.0.0.0:22" : "explicit address/port",
           SSHD_MAX_SESSIONS);
    printf("SSHD listening; host key=%s (press Ctrl+C to stop)\n",
           hostkey_path[0] != '\0' ? hostkey_path : "configured default");

    memset(&stop_action, 0, sizeof(stop_action));
    stop_action.sa_handler = sshd_stop_handler;
    sigemptyset(&stop_action.sa_mask);
    g_sshd_stop_requested = 0;

    if (sigaction(SIGINT, &stop_action, &old_sigint) == 0) {
        sigint_installed = 1;
    }

    if (sigaction(SIGTERM, &stop_action, &old_sigterm) == 0) {
        sigterm_installed = 1;
    }

    while (!g_sshd_stop_requested) {
        session = ssh_new();
        if (session == NULL) {
            fprintf(stderr, "Failed to allocate session\n");
            continue;
        }

        /* Blocks until there is a new incoming connection. */
        if(ssh_bind_accept(sshbind, session) != SSH_ERROR) {
#ifdef WITH_FORK
            ssh_event event;

            switch(fork()) {
                case 0:
                    /* Remove the SIGCHLD handler inherited from parent. */
                    sa.sa_handler = SIG_DFL;
                    sigaction(SIGCHLD, &sa, NULL);
                    /* Remove socket binding, which allows us to restart the
                     * parent process, without terminating existing sessions. */
                    ssh_bind_free(sshbind);

                    event = ssh_event_new();
                    if (event != NULL) {
                        /* Blocks until the SSH session ends by either
                         * child process exiting, or client disconnecting. */
                        handle_session(event, session);
                        ssh_event_free(event);
                    } else {
                        fprintf(stderr, "Could not create polling context\n");
                    }
                    ssh_disconnect(session);
                    ssh_free(session);

                    exit(0);
                case -1:
                    fprintf(stderr, "Failed to fork\n");
            }
#else
            pthread_t tid;
            int slot;

            pthread_mutex_lock(&g_sessions_lock);
            for (slot = 0; slot < SSHD_MAX_SESSIONS; slot++) {
                if (g_sessions[slot] == NULL) {
                    g_sessions[slot] = session;
                    break;
                }
            }
            pthread_mutex_unlock(&g_sessions_lock);
            if (slot == SSHD_MAX_SESSIONS) {
                syslog(LOG_WARNING, "SSHD: session limit reached\n");
                ssh_disconnect(session);
                ssh_free(session);
                continue;
            }
#ifdef THREAD_STACKSIZE
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setstacksize(&attr, THREAD_STACKSIZE);
            rc = pthread_create(&tid, &attr, session_thread, session);
            pthread_attr_destroy(&attr);
#else
            rc = pthread_create(&tid, NULL, session_thread, session);
#endif
            if (rc == 0) {
                pthread_detach(tid);
                continue;
            }
            pthread_mutex_lock(&g_sessions_lock);
            g_sessions[slot] = NULL;
            pthread_mutex_unlock(&g_sessions_lock);
            fprintf(stderr, "Failed to pthread_create\n");
#endif
        } else {
            if (!g_sshd_stop_requested) {
                fprintf(stderr, "%s\n", ssh_get_error(sshbind));
            }
        }
        /* Since the session has been passed to a child fork, do some cleaning
         * up at the parent process. */
        ssh_disconnect(session);
        ssh_free(session);
    }

    if (sigterm_installed) {
        sigaction(SIGTERM, &old_sigterm, NULL);
    }
    if (sigint_installed) {
        sigaction(SIGINT, &old_sigint, NULL);
    }

    ssh_bind_free(sshbind);
#ifndef WITH_FORK
    /* Workers still use libssh/crypto and the shared credentials.  Close
     * their transports and let them unwind before finalizing the library. */
    pthread_mutex_lock(&g_sessions_lock);
    g_server_listening = 0;
    for (int i = 0; i < SSHD_MAX_SESSIONS; i++) {
        if (g_sessions[i] != NULL) {
            shutdown(ssh_get_fd(g_sessions[i]), SHUT_RDWR);
        }
    }
    for (;;) {
        int active = 0;
        for (int i = 0; i < SSHD_MAX_SESSIONS; i++) {
            active += g_sessions[i] != NULL;
        }
        if (active == 0) {
            break;
        }
        pthread_cond_wait(&g_sessions_done, &g_sessions_lock);
    }
    pthread_mutex_unlock(&g_sessions_lock);
#endif
    ssh_finalize();
    explicit_bzero(password, sizeof(password));
    printf("SSHD stopped; listening socket released\n");
    return 0;
}

int main(int argc, char **argv) {
    int rc;
#ifdef CONFIG_SYSTEM_A733SERVICES
    if (argc == 3 && strcmp(argv[1], "--autostart") == 0) {
        rc = a733_auto_option("ssh", argv[2]);
        if (rc < 0) fprintf(stderr, "sshd autostart: %s (%d)\n", strerror(-rc), rc);
        return rc < 0 ? 1 : 0;
    }
#endif
    if (argc == 2 && strcmp(argv[1], "--status") == 0) {
        int active = 0;
        pthread_mutex_lock(&g_sessions_lock);
        for (int i = 0; i < SSHD_MAX_SESSIONS; i++) {
            active += g_sessions[i] != NULL;
        }
        printf("SSHD: pid=%d listening=%d sessions=%d/%d\n",
               (int)g_server_pid, g_server_listening, active, SSHD_MAX_SESSIONS);
        pthread_mutex_unlock(&g_sessions_lock);
#ifdef CONFIG_SYSTEM_A733SERVICES
        a733_auto_option("ssh", "status");
#endif
        return 0;
    }
    if (pthread_mutex_trylock(&g_server_lock) != 0) {
        fprintf(stderr, "SSHD already active; use 'sshd --status'. "
                "Stop it before reconfiguring authentication.\n");
        return 1;
    }
    pthread_mutex_lock(&g_sessions_lock);
    g_server_pid = getpid();
    pthread_mutex_unlock(&g_sessions_lock);
    rc = sshd_run(argc, argv);
    pthread_mutex_lock(&g_sessions_lock);
    g_server_pid = 0;
    g_server_listening = 0;
    pthread_mutex_unlock(&g_sessions_lock);
    if (rc != 0) {
        syslog(LOG_ERR, "SSHD: exited status=%d; inspect preceding error\n", rc);
    }
    pthread_mutex_unlock(&g_server_lock);
    return rc;
}
