#include <stdio.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "tcg-op.h"
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "fuzz/hfuzz.h"



#define BUFFER_SIZE 0x500

target_ulong hfuzz_qemu_start_code;
target_ulong hfuzz_qemu_end_code;


/**
 * Point at which the memory and register dump should be loaded
 */
target_ulong hfuzz_qemu_setup_point = 0x40000450;

/**
 * Point, where the memory dump was taken
 */
target_ulong hfuzz_qemu_entry_point = 0;

/**
 * The register which holds the length of the input data. 
 */
#define MAX_REGISTERS	16
int hfuzz_length_registers[MAX_REGISTERS];

/**
 * The location, where the input data is located.
 */ 
target_ulong hfuzz_input_data_pointer = 0;

#define MAX_EXIT_POINTS 10
/**
 * Points where to exit the execution if child flag is set
 */ 
target_ulong hfuzz_qemu_exit_points[MAX_EXIT_POINTS];

size_t hfuzz_qemu_n_exit_points = 0;

char * hfuzz_dump_file = 0;
char * hfuzz_regs_file = 0;

//http request ending
//const char *http_crlf = " HTTP/1.1\r\nHost: 123.0.0.1\r\n\r\n";
const char *http_crlf = "\r\n";

extern void HonggfuzzFetchData(const uint8_t** buf_ptr, size_t* len_ptr);
extern void hfuzzInstrumentInit(void);

//Whitebox fuzzing
#define TARGET_ADDRESS "localhost"
int TARGET_PORT = 8081;
int fuzz_mode = 0;

/**
 * send data to target, check liveness
 * */
void sendOneRequest(const uint8_t *fuzz_string, size_t fuzz_len);
void sendOneRequest(const uint8_t *fuzz_string, size_t fuzz_len)
{

    //socket to target
    int target_sockfd;
    struct sockaddr_in target_sock_addr;
    struct hostent *hp;

    //Send and receive buffer
    char buf[BUFFER_SIZE]; //TODO Determine buffer size

    if (fuzz_len > BUFFER_SIZE - strlen(http_crlf))
    {
        printf("Fuzzer data too big!\n");
        return;
    }

    //concatenate fuzzer string + http header and save to buf
    //snprintf(buf, sizeof buf, "%s%s", fuzz_string, http_crlf);
    memcpy(buf, fuzz_string, fuzz_len);
    memcpy(buf + fuzz_len, http_crlf, 2);

    //create socket to target
    target_sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (target_sockfd < 0)
    {
        perror("ERROR opening target socket");
        exit(1);
    }

    struct timeval tv;
    tv.tv_sec = 8;
    tv.tv_usec = 0;
    if (setsockopt(target_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv))
    {
        perror("ERROR set target socket option");
        exit(1);
    }

    int val = 1;
    if (setsockopt(target_sockfd, SOL_SOCKET, SO_REUSEADDR, &val, (socklen_t)sizeof(val)) == -1)
    {
        perror("ERROR set target socket option");
    }

    val = 1;
    if (setsockopt(target_sockfd, SOL_TCP, TCP_NODELAY, &val, (socklen_t)sizeof(val)) == -1)
    {
        perror("ERROR set target socket option");
        exit(1);
    }

    val = 1;
    if (setsockopt(target_sockfd, SOL_TCP, TCP_QUICKACK, &val, (socklen_t)sizeof(val)) == -1)
    {
        perror("ERROR set target socket option");
        exit(1);
    }

    /* get internet address of host specified by command line */
    hp = gethostbyname(TARGET_ADDRESS);
    if (hp == NULL)
    {
        perror("ERROR resolve target");
        exit(1);
    }

    bzero(&target_sock_addr, sizeof(target_sock_addr));
    target_sock_addr.sin_family = AF_INET;
    target_sock_addr.sin_port = htons(TARGET_PORT);
    bcopy(hp->h_addr, &target_sock_addr.sin_addr, hp->h_length);

    //Connect to target
    if (connect(target_sockfd, (struct sockaddr *)&target_sock_addr, sizeof(target_sock_addr)) < 0)
    {
        perror("connect to target failed. Error");
        exit(1);
    }

    //send request
    int len = send(target_sockfd, buf, strlen(buf) + 1, 0);
    //printf("%i \n", len);

    if(len < 0) {
      perror("send failed. Error");
      raise(SIGSEGV); //probably fault
    }

    pthread_yield();
    //Read response to avoid full buffers of target

    read(target_sockfd, buf, BUFFER_SIZE);


    close(target_sockfd);
}

static void whitebox_fuzzing(CPUState *cpu) {

  pid_t pid = fork();
  if (pid < 0) {
    fputs("fork error\n", stderr);
    exit(1);
  }

  // Child
  if (!pid) {

    const uint8_t *fuzz_string = (uint8_t *)"";
    size_t fuzz_len = 0;

    hfuzzInstrumentInit();

    //wait for target to come up
    //sleep(4);

    // //get fuzzing string from honggfuzz
    while (1) {
      HonggfuzzFetchData(&fuzz_string, &fuzz_len);
      sendOneRequest(fuzz_string, fuzz_len);
    }
    exit(1);
  } else {
    // Parent
    return;
  }

}

//Blackbox Fuzzing
#define USE_FORKSERVER 1
/**
 * Checks if request in guest buffer is a GET request
 */
bool isGetRequest(CPUState *cpu);
bool isGetRequest(CPUState *cpu) {
  uint8_t a[10];
  uint64_t uri_pointer = hfuzz_input_data_pointer;
  address_space_read(cpu->as, uri_pointer, MEMTXATTRS_UNSPECIFIED, a,10 );
  if(strncmp((char*)a, "qqq", 3) == 0) {
    return true;
  }
    return false;
}

/**
 * Fetches fuzzing data and writes it to guest buffer
 */
void injectFuzzingData(CPUState *cpu);
void injectFuzzingData(CPUState *cpu) {

  const uint8_t *fuzz_string;
  size_t fuzz_len = 0 ;

  HonggfuzzFetchData(&fuzz_string, &fuzz_len);

  char buf[BUFFER_SIZE];
  memset(buf, 0, BUFFER_SIZE);

  if (fuzz_len > BUFFER_SIZE - strlen(http_crlf) + 1)
  {
      printf("Fuzzer data too big!\n");
      fuzz_len = BUFFER_SIZE - strlen(http_crlf) + 1;
  }

  memcpy(buf, fuzz_string, fuzz_len);
  memcpy(buf + fuzz_len, http_crlf, 2);

  uint32_t data_len = fuzz_len + strlen(http_crlf) +1;

  uint64_t uri_pointer = hfuzz_input_data_pointer;

  address_space_rw(cpu->as, uri_pointer, MEMTXATTRS_UNSPECIFIED, (uint8_t *)buf, data_len, true);

  for (int i = 0; hfuzz_length_registers[i] >= 0; i++)
    ((CPUArchState *) cpu->env_ptr)->regs[hfuzz_length_registers[i]] = data_len;
}


static void fork_server(CPUState *cpu) {
  hfuzzInstrumentInit();

  while (1) {
    pid_t pid = fork();
    if (pid < 0) {
      fputs("fork error\n", stderr);
      exit(1);
    }

    if (pid) {
      int status;
      if (waitpid(pid, &status, 0) <= 0) {
        fputs("waitpid error\n", stderr);
        exit(1);
      }
      if(status == 1 ) {
        raise(SIGSEGV);
      }
    } else {
      childProcess = true;
      injectFuzzingData(cpu);
      return;
    }
  }
}




void hfuzz_qemu_setup(CPUState *cpu) {
    switch(fuzz_mode) {
    case FUZZ_WHITEBOX:
        whitebox_fuzzing(cpu);
        break;
    case FUZZ_BLACKBOX:
        fork_server(cpu);
        break;
    }
}

extern void hfuzz_trace_cmp4(uintptr_t pc, uint64_t Arg1, uint64_t Arg2);
extern void hfuzz_trace_cmp8(uintptr_t pc, uint64_t Arg1, uint64_t Arg2);

void HELPER(hfuzz_qemu_trace_cmp_i64)(
	uint64_t cur_loc, uint64_t arg1, uint64_t arg2
    ) {
    if (cur_loc > hfuzz_qemu_end_code || cur_loc < hfuzz_qemu_start_code) {
        return;
    }
    hfuzz_trace_cmp4(cur_loc, arg1, arg2);
    hfuzz_trace_cmp8(cur_loc, arg1, arg2);
}

void HELPER(hfuzz_qemu_trace_cmp_i32)(
	uint32_t cur_loc, uint32_t arg1, uint32_t arg2
    ) {
    if (cur_loc > hfuzz_qemu_end_code || cur_loc < hfuzz_qemu_start_code) {
        return;
    }
    hfuzz_trace_cmp4(cur_loc, arg1, arg2);
}

QemuOptsList qemu_blackbox_opts = {
    .name = "blackbox",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_blackbox_opts.head),
    .desc = {
        {
            .name = "setup",
            .type = QEMU_OPT_STRING,
        },{
            .name = "entry",
            .type = QEMU_OPT_STRING,
        },{
            .name = "exit",
            .type = QEMU_OPT_STRING,
        },{
            .name = "len",
            .type = QEMU_OPT_STRING,
        },{
            .name = "data",
            .type = QEMU_OPT_STRING,
        },{
            .name = "dump_file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "regs_file",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

QemuOptsList qemu_whitebox_opts = {
    .name = "whitebox",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_whitebox_opts.head),
    .desc = {
        {
            .name = "init",
            .type = QEMU_OPT_STRING,
        },{
            .name = "start",
            .type = QEMU_OPT_STRING,
        },{
            .name = "end",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

int qemu_blackbox_option(const char *str)
{
    QemuOpts *opts;

    opts = qemu_opts_parse_noisily(&qemu_blackbox_opts, str, false);
    if (!opts) {
        return -1;
    }

    const char * opt_setup = qemu_opt_get(opts, "setup");
    if (opt_setup == NULL) {
        printf("fuzzer setup missing!\n");
        return -1;
    }
    hfuzz_qemu_setup_point = strtol(opt_setup, 0, 0);
    if(errno == ERANGE) {
        printf("parsing setup location failed\n");
        return -1;
    }

    const char * opt_entry = qemu_opt_get(opts, "entry");
    if (opt_entry == NULL) {
        printf("fuzzer entry missing!\n");
        return -1;
    }
    hfuzz_qemu_entry_point = strtol(opt_entry, 0, 0);
    if(errno == ERANGE) {
        printf("parsing entry location failed\n");
        return -1;
    }

    char * opt_exit = (char *) qemu_opt_get(opts, "exit");
    if (opt_exit == NULL) {
        printf("fuzzer exit missing!\n");
        return -1;
    }

    opt_exit = strtok(opt_exit, "+");
    int i;
    for (i = 0; i < MAX_EXIT_POINTS && opt_exit != NULL; i++) {
      hfuzz_qemu_exit_points[i] = strtol(opt_exit, 0, 0);
      if(errno == ERANGE) {
          printf("parsing exit location failed\n");
          return -1;
      }
      opt_exit = strtok(NULL, "+");
    }

    hfuzz_qemu_n_exit_points = i;
    if (hfuzz_qemu_n_exit_points == 0) {
        printf("at least one exit point required\n");
        return -1;
    }

    memset(hfuzz_length_registers, -1, MAX_REGISTERS * sizeof(int));
    char *opt_len = (char *)qemu_opt_get(opts, "len");
    if (opt_len == NULL) {
        printf("fuzzer len missing!\n");
        return -1;
    }
    opt_len = strtok(opt_len, "+");
    for (i = 0; i < MAX_REGISTERS && opt_len != NULL; i++) {
      hfuzz_length_registers[i] = strtol(opt_len + 1, 0, 0);
      if(errno == ERANGE) {
          printf("parsing length register number failed\n");
          return -1;
      }
      opt_len = strtok(NULL, "+");
    }

    const char * opt_data = qemu_opt_get(opts, "data");
    if (opt_data == NULL) {
        printf("fuzzer data missing!\n");
        return -1;
    }
    hfuzz_input_data_pointer = strtol(opt_data, 0, 0);
    if(errno == ERANGE) {
        printf("parsing data pointer failed\n");
        return -1;
    }

    hfuzz_dump_file = (char *) qemu_opt_get(opts, "dump_file");
    if (hfuzz_dump_file == NULL) {
        printf("fuzzer dump_file missing!\n");
        return -1;
    }

    hfuzz_regs_file = (char *) qemu_opt_get(opts, "regs_file");
    if (hfuzz_regs_file == NULL) {
        printf("fuzzer regs_file missing!\n");
        return -1;
    }

    fuzz_mode = FUZZ_BLACKBOX;

    return 0;
}

int qemu_whitebox_option(const char *str)
{
    QemuOpts *opts;
    const char *opt_start, *opt_end;

    opts = qemu_opts_parse_noisily(&qemu_whitebox_opts, str, false);
    if (!opts) {
        return -1;
    }

    const char * opt_init = qemu_opt_get(opts, "init");
    if (opt_init == NULL) {
        printf("fuzzer init missing!\n");
        return -1;
    }
    hfuzz_qemu_setup_point = strtol(opt_init, 0, 0);
    if(errno == ERANGE) {
        printf("parsing init location failed\n");
        return -1;
    }

    opt_start = qemu_opt_get(opts, "start");
    if (opt_start == NULL) {
        printf("fuzzer start missing!\n");
        return -1;
    }
    hfuzz_qemu_start_code = strtol(opt_start, 0, 0);
    if(errno == ERANGE) {
        printf("parsing start location failed\n");
        return -1;
    }

    opt_end = qemu_opt_get(opts, "end");
    if (opt_end == NULL) {
        printf("fuzzer end missing!\n");
        return -1;
    }
    hfuzz_qemu_end_code = strtol(opt_end, 0, 0);
    if(errno == ERANGE) {
        printf("parsing start location failed\n");
        return -1;
    }

    fuzz_mode = FUZZ_WHITEBOX;

    return 0;
}
