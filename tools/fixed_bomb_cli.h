/* `carlobomb fixed ...`: minimum-node REVs looked up in a FixedRunBomb. */
#ifndef CARLOBOMB_FIXED_BOMB_CLI_H
#define CARLOBOMB_FIXED_BOMB_CLI_H

/* argv[1] is "fixed".  Returns the process exit status. */
int fixed_bomb_cli(int argc, char **argv);

/* The lines `carlobomb help` prints for the command. */
void fixed_bomb_cli_usage(void);

#endif
