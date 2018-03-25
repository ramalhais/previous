/*
  Hatari - debug.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Internal header used by debugger files.
*/
#ifndef HATARI_DEBUG_PRIV_H
#define HATARI_DEBUG_PRIV_H

/* DebugUI command structure */
typedef struct
{
	int (*pFunction)(int argc, char *argv[]);
	char* (*pMatch)(const char *, int);
	const char *sLongName;
	const char *sShortName;
	const char *sShortDesc;
	const char *sUsage;
	bool bNoParsing;
} dbgcommand_t;

/* Output file debugger output */
extern FILE *debugOutput;

extern void DebugUI_PrintCmdHelp(const char *psCmd);

extern int DebugCpu_Init(const dbgcommand_t **table);
extern void DebugCpu_InitSession(void);

extern int DebugDsp_Init(const dbgcommand_t **table);
extern void DebugDsp_InitSession(void);

#endif /* HATARI_DEBUG_PRIV_H */