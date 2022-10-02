/*
  Hatari - log.h
  
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_LOG_H
#define HATARI_LOG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
    
#include <stdbool.h>


/* Logging
 * -------
 * Is always enabled as it's information that can be useful
 * to the Hatari users
 */
typedef enum
{
    LOG_NONE,   /* invalid LOG level */
/* these present user with a dialog and log the issue */
	LOG_FATAL,	/* Hatari can't continue unless user resolves issue */
	LOG_ERROR,	/* something user did directly failed (e.g. save) */
/* these just log the issue */
	LOG_WARN,	/* something failed, but it's less serious */
	LOG_INFO,	/* user action success (e.g. TOS file load) */
	LOG_TODO,	/* functionality not yet being emulated */
	LOG_DEBUG,	/* information about internal Hatari working */
} LOGTYPE;

#ifndef __GNUC__
/* assuming attributes work only for GCC */
#define __attribute__(foo)
#endif

extern int Log_Init(void);
extern int Log_SetAlertLevel(int level);
extern void Log_UnInit(void);
extern void _Log_Printf(LOGTYPE nType, const char *psFormat, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void Log_AlertDlg(LOGTYPE nType, const char *psFormat, ...)
	__attribute__ ((format (printf, 2, 3)));
extern LOGTYPE Log_ParseOptions(const char *OptionStr);
extern const char* Log_SetTraceOptions(const char *OptionsStr);
extern char *Log_MatchTrace(const char *text, int state);
    
#ifndef __GNUC__
#undef __attribute__
#endif

#define _Log_LOG_FATAL(nType, psFormat, ...) _Log_Printf(nType, psFormat, ## __VA_ARGS__)
#define _Log_LOG_ERROR(nType, psFormat, ...) _Log_Printf(nType, psFormat, ## __VA_ARGS__)
#define _Log_LOG_WARN(nType, psFormat, ...)  _Log_Printf(nType, psFormat, ## __VA_ARGS__)
#define _Log_LOG_INFO(nType, psFormat, ...)
#define _Log_LOG_TODO(nType, psFormat, ...)
#define _Log_LOG_DEBUG(nType, psFormat, ...)
#define _Log_LOG_NONE(nType, psFormat, ...)
    
#define LOG_LEVEL_COMBINE(prefix, nType, psFormat, ...) prefix ## nType (nType, psFormat, ## __VA_ARGS__)
#define Log_Printf(nType, psFormat, ...) LOG_LEVEL_COMBINE(_Log_,nType, psFormat, ## __VA_ARGS__)

/* Tracing
 * -------
 * Tracing outputs information about what happens in the emulated
 * system and slows down the emulation.  As it's intended mainly
 * just for the Hatari developers, tracing support is compiled in
 * by default.
 * 
 * Tracing can be enabled by defining ENABLE_TRACING
 * in the top level config.h
 */
#include "config.h"

/* Up to 64 levels when using uint32_t for HatariTraceFlags */

#define	TRACE_MFP_EXCEPTION	 (1<<9)
#define	TRACE_MFP_START 	 (1<<10)
#define	TRACE_MFP_READ  	 (1<<11)
#define	TRACE_MFP_WRITE 	 (1<<12)

#define	TRACE_PSG_READ  	 (1<<13)
#define	TRACE_PSG_WRITE 	 (1<<14)

#define	TRACE_CPU_PAIRING	 (1<<15)
#define	TRACE_CPU_DISASM	 (1<<16)
#define	TRACE_CPU_EXCEPTION	 (1<<17)

#define	TRACE_INT		 (1<<18)

#define	TRACE_FDC		 (1<<19)

#define	TRACE_IKBD_CMDS 	 (1<<20)
#define	TRACE_IKBD_ACIA 	 (1<<21)
#define	TRACE_IKBD_EXEC 	 (1<<22)

#define TRACE_IOMEM_RD  	 (1<<29)
#define TRACE_IOMEM_WR  	 (1<<30)

#define TRACE_DSP_HOST_INTERFACE (1ll<<34)
#define TRACE_DSP_HOST_COMMAND	 (1ll<<35)
#define TRACE_DSP_HOST_SSI	 (1ll<<36)
#define TRACE_DSP_DISASM	 (1ll<<37)
#define TRACE_DSP_DISASM_REG	 (1ll<<38)
#define TRACE_DSP_DISASM_MEM	 (1ll<<39)
#define TRACE_DSP_STATE		 (1ll<<40)
#define TRACE_DSP_INTERRUPT	 (1ll<<41)

#define	TRACE_NONE		 (0)
#define	TRACE_ALL		 (~0)

#define	TRACE_PSG_ALL		( TRACE_PSG_READ | TRACE_PSG_WRITE )

#define	TRACE_CPU_ALL		( TRACE_CPU_PAIRING | TRACE_CPU_DISASM | TRACE_CPU_EXCEPTION )

#define	TRACE_IOMEM_ALL		( TRACE_IOMEM_RD | TRACE_IOMEM_WR )

#define TRACE_DSP_ALL		( TRACE_DSP_HOST_INTERFACE | TRACE_DSP_HOST_COMMAND | TRACE_DSP_HOST_SSI | TRACE_DSP_DISASM \
    | TRACE_DSP_DISASM_REG | TRACE_DSP_DISASM_MEM | TRACE_DSP_STATE | TRACE_DSP_INTERRUPT )

/* Dummy for DSP */
#define ExceptionDebugMask 0
#define EXCEPT_DSP 0


extern FILE *TraceFile;
extern uint64_t LogTraceFlags;

#if ENABLE_TRACING

#ifndef _VCWIN_
#define	LOG_TRACE(level, args...) \
	if (unlikely(LogTraceFlags & level)) fprintf(TraceFile, args)
#endif
#define LOG_TRACE_LEVEL( level )	(unlikely(LogTraceFlags & level))

#else		/* ENABLE_TRACING */

#ifndef _VCWIN_
#define LOG_TRACE(level, args...)	{}
#endif
#define LOG_TRACE_LEVEL( level )	(0)

#endif		/* ENABLE_TRACING */

/* Always defined in full to avoid compiler warnings about unused variables.
 * In code it's used in such a way that it will be optimized away when tracing
 * is disabled.
 */
#ifndef _VCWIN_
#define LOG_TRACE_PRINT(args...)	fprintf(TraceFile , args)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */
        
#endif		/* HATARI_LOG_H */
