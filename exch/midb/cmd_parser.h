#pragma once
#include <gromox/double_list.hpp>
#include <pthread.h>


struct CONNECTION {
	DOUBLE_LIST_NODE node;
	int sockd;
	BOOL is_selecting;
	pthread_t thr_id;
};

typedef int (*COMMAND_HANDLER)(int argc, char** argv, int sockd);

void cmd_parser_init(int threads_num, int timeout);
extern int cmd_parser_run(void);
extern int cmd_parser_stop(void);
extern void cmd_parser_free(void);
extern CONNECTION *cmd_parser_get_connection(void);
void cmd_parser_put_connection(CONNECTION *pconnection);

void cmd_parser_register_command(const char *command, COMMAND_HANDLER handler);
