#ifndef BPE_SQUIRREL_CONSOLE_H
#define BPE_SQUIRREL_CONSOLE_H

/* ================================================================
 * Squirrel Console Module
 *
 * In-game Squirrel (.nut) scripting console.
 * Attempts to find the game's sq_call / sq_compile functions via
 * import or pattern scan.  Falls back to a plain text console
 * that prints commands to the log for manual execution.
 * ================================================================ */

void squirrel_console_init(void);
void squirrel_console_render(void);   /* ImGui window — call inside Present */
void squirrel_console_execute(const char *cmd);

#endif /* BPE_SQUIRREL_CONSOLE_H */
