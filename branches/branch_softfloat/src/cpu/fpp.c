/*
 * UAE - The Un*x Amiga Emulator
 *
 * MC68881/68882/68040/68060 FPU emulation
 *
 * Copyright 1996 Herman ten Brugge
 * Modified 2005 Peter Keunecke
 * 68040+ exceptions and more by Toni Wilen
 */

#include "main.h"
#include "hatari-glue.h"


#include "sysconfig.h"
#include "sysdeps.h"


#include "options_cpu.h"
#include "memory.h"
#include "newcpu.h"
#include "cpummu.h"
#include "cpummu030.h"

#ifdef WITH_SOFTFLOAT
#include "fpp-softfloat.h"
#else
#include "md-fpp.h"
#endif

#define DEBUG_FPP 0
#define EXCEPTION_FPP 1

uae_u32 xhex_pi[]    ={0x40000000, 0xc90fdaa2, 0x2168c235};
uae_u32 xhex_exp_1[] ={0x40000000, 0xadf85458, 0xa2bb4a9a};
uae_u32 xhex_l2_e[]  ={0x3fff0000, 0xb8aa3b29, 0x5c17f0bc};
uae_u32 xhex_ln_2[]  ={0x3ffe0000, 0xb17217f7, 0xd1cf79ac};
uae_u32 xhex_ln_10[] ={0x40000000, 0x935d8ddd, 0xaaa8ac17};
uae_u32 xhex_l10_2[] ={0x3ffd0000, 0x9a209a84, 0xfbcff798};
uae_u32 xhex_l10_e[] ={0x3ffd0000, 0xde5bd8a9, 0x37287195};
uae_u32 xhex_zero[]  ={0x00000000, 0x00000000, 0x00000000};
uae_u32 xhex_1e0[]   ={0x3fff0000, 0x80000000, 0x00000000};
uae_u32 xhex_1e1[]   ={0x40020000, 0xa0000000, 0x00000000};
uae_u32 xhex_1e2[]   ={0x40050000, 0xc8000000, 0x00000000};
uae_u32 xhex_1e4[]   ={0x400c0000, 0x9c400000, 0x00000000};
uae_u32 xhex_1e8[]   ={0x40190000, 0xbebc2000, 0x00000000};
uae_u32 xhex_1e16[]  ={0x40340000, 0x8e1bc9bf, 0x04000000};
uae_u32 xhex_1e32[]  ={0x40690000, 0x9dc5ada8, 0x2b70b59e};
uae_u32 xhex_1e64[]  ={0x40d30000, 0xc2781f49, 0xffcfa6d5};
uae_u32 xhex_1e128[] ={0x41a80000, 0x93ba47c9, 0x80e98ce0};
uae_u32 xhex_1e256[] ={0x43510000, 0xaa7eebfb, 0x9df9de8e};
uae_u32 xhex_1e512[] ={0x46a30000, 0xe319a0ae, 0xa60e91c7};
uae_u32 xhex_1e1024[]={0x4d480000, 0xc9767586, 0x81750c17};
uae_u32 xhex_1e2048[]={0x5a920000, 0x9e8b3b5d, 0xc53d5de5};
uae_u32 xhex_1e4096[]={0x75250000, 0xc4605202, 0x8a20979b};

uae_u32 xhex_nan[]   ={0x7fff0000, 0xffffffff, 0xffffffff};

static bool fpu_mmu_fixup;


void to_single(fptype *fp, uae_u32 wrd1)
{
    // automatically fix denormals if 6888x
    if (currprefs.fpu_model == 68881 || currprefs.fpu_model == 68882)
        to_single_xn(fp, wrd1);
    else
        to_single_x(fp, wrd1);
}
uae_u32 from_single(fptype *fp)
{
    return from_single_x(fp);
}
void to_double(fptype *fp, uae_u32 wrd1, uae_u32 wrd2)
{
    // automatically fix denormals if 6888x
    if (currprefs.fpu_model == 68881 || currprefs.fpu_model == 68882)
        to_double_xn(fp, wrd1, wrd2);
    else
        to_double_x(fp, wrd1, wrd2);
}
void from_double(fptype *fp, uae_u32 *wrd1, uae_u32 *wrd2)
{
    from_double_x(fp, wrd1, wrd2);
}
void to_exten(fptype *fp, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    // automatically fix unnormals if 6888x
    if (currprefs.fpu_model == 68881 || currprefs.fpu_model == 68882) {
        normalize_exten(&wrd1, &wrd2, &wrd3);
    }
    to_exten_x(fp, wrd1, wrd2, wrd3);
}
void to_exten_fmovem(fptype *fp, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
    to_exten_x(fp, wrd1, wrd2, wrd3);
}
void from_exten(fptype *fp, uae_u32 *wrd1, uae_u32 *wrd2, uae_u32 *wrd3)
{
    from_exten_x(fp, wrd1, wrd2, wrd3);
}

/* Floating Point Control Register (FPCR)
 *
 * Exception Enable Byte
 * x--- ---- ---- ----  bit 15: BSUN (branch/set on unordered)
 * -x-- ---- ---- ----  bit 14: SNAN (signaling not a number)
 * --x- ---- ---- ----  bit 13: OPERR (operand error)
 * ---x ---- ---- ----  bit 12: OVFL (overflow)
 * ---- x--- ---- ----  bit 11: UNFL (underflow)
 * ---- -x-- ---- ----  bit 10: DZ (divide by zero)
 * ---- --x- ---- ----  bit 9: INEX 2 (inexact operation)
 * ---- ---x ---- ----  bit 8: INEX 1 (inexact decimal input)
 *
 * Mode Control Byte
 * ---- ---- xx-- ----  bits 7 and 6: PREC (rounding precision)
 * ---- ---- --xx ----  bits 5 and 4: RND (rounding mode)
 * ---- ---- ---- xxxx  bits 3 to 0: all 0
 */

#define FPCR_PREC   0x00C0
#define FPCR_RND    0x0030

/* Floating Point Status Register (FPSR)
 *
 * Condition Code Byte
 * xxxx ---- ---- ---- ---- ---- ---- ----  bits 31 to 28: all 0
 * ---- x--- ---- ---- ---- ---- ---- ----  bit 27: N (negative)
 * ---- -x-- ---- ---- ---- ---- ---- ----  bit 26: Z (zero)
 * ---- --x- ---- ---- ---- ---- ---- ----  bit 25: I (infinity)
 * ---- ---x ---- ---- ---- ---- ---- ----  bit 24: NAN (not a number or unordered)
 *
 * Quotient Byte (set and reset only by FMOD and FREM)
 * ---- ---- x--- ---- ---- ---- ---- ----  bit 23: sign of quotient
 * ---- ---- -xxx xxxx ---- ---- ---- ----  bits 22 to 16: 7 least significant bits of quotient
 *
 * Exception Status Byte
 * ---- ---- ---- ---- x--- ---- ---- ----  bit 15: BSUN (branch/set on unordered)
 * ---- ---- ---- ---- -x-- ---- ---- ----  bit 14: SNAN (signaling not a number)
 * ---- ---- ---- ---- --x- ---- ---- ----  bit 13: OPERR (operand error)
 * ---- ---- ---- ---- ---x ---- ---- ----  bit 12: OVFL (overflow)
 * ---- ---- ---- ---- ---- x--- ---- ----  bit 11: UNFL (underflow)
 * ---- ---- ---- ---- ---- -x-- ---- ----  bit 10: DZ (divide by zero)
 * ---- ---- ---- ---- ---- --x- ---- ----  bit 9: INEX 2 (inexact operation)
 * ---- ---- ---- ---- ---- ---x ---- ----  bit 8: INEX 1 (inexact decimal input)
 *
 * Accrued Exception Byte
 * ---- ---- ---- ---- ---- ---- x--- ----  bit 7: IOP (invalid operation)
 * ---- ---- ---- ---- ---- ---- -x-- ----  bit 6: OVFL (overflow)
 * ---- ---- ---- ---- ---- ---- --x- ----  bit 5: UNFL (underflow)
 * ---- ---- ---- ---- ---- ---- ---x ----  bit 4: DZ (divide by zero)
 * ---- ---- ---- ---- ---- ---- ---- x---  bit 3: INEX (inexact)
 * ---- ---- ---- ---- ---- ---- ---- -xxx  bits 2 to 0: all 0
 */

#define FPSR_ZEROBITS   0xF0000007

#define FPSR_CC_N       0x08000000
#define FPSR_CC_Z       0x04000000
#define FPSR_CC_I       0x02000000
#define FPSR_CC_NAN     0x01000000

#define FPSR_QUOT_SIGN  0x00800000
#define FPSR_QUOT_LSB   0x007F0000

#define FPSR_BSUN       0x00008000
#define FPSR_SNAN       0x00004000
#define FPSR_OPERR      0x00002000
#define FPSR_OVFL       0x00001000
#define FPSR_UNFL       0x00000800
#define FPSR_DZ         0x00000400
#define FPSR_INEX2      0x00000200
#define FPSR_INEX1      0x00000100

#define FPSR_AE_IOP     0x00000080
#define FPSR_AE_OVFL    0x00000040
#define FPSR_AE_UNFL    0x00000020
#define FPSR_AE_DZ      0x00000010
#define FPSR_AE_INEX    0x00000008


static void fpnan (fptype *fp)
{
    to_exten(fp, xhex_nan[0], xhex_nan[1], xhex_nan[2]);
}

static void fpclear (fptype *fp)
{
    *fp = from_int(0);
}
static void fpset (fptype *fp, uae_s32 val)
{
    *fp = from_int(val);
}

#if 0
bool float_is_denormal(uae_u32 wrd1)
{
    uae_u16 exp = (wrd1 >> 23) & 0xff;
    
    if (exp == 0 && (wrd1 & 0x007fffff)) {
        return true;
    }
    return false;
}
bool double_is_denormal(uae_u32 wrd1, uae_u32 wrd2)
{
    uae_u16 exp = (wrd1 >> 20) & 0x7ff;
    
    if (exp == 0 && ((wrd1 & 0x000fffff) || wrd2)) {
        return true;
    }
    return false;
}
#endif
void normalize_exten(uae_u32 *pwrd1, uae_u32 *pwrd2, uae_u32 *pwrd3)
{
    uae_u32 wrd1 = *pwrd1;
    uae_u32 wrd2 = *pwrd2;
    uae_u32 wrd3 = *pwrd3;
    uae_u16 exp = (wrd1 >> 16) & 0x7fff;
    // Normalize if unnormal.
    if (exp != 0 && exp != 0x7fff && !(wrd2 & 0x80000000)) {
        while (!(wrd2 & 0x80000000) && (wrd2 || wrd3)) {
            if (exp == 0)
                break; // Result is denormal
            wrd2 <<= 1;
            if (wrd3 & 0x80000000)
                wrd2 |= 1;
            wrd3 <<= 1;
            exp--;
        }
        if (!wrd2 && !wrd3)
            exp = 0;
        *pwrd1 = (wrd1 & 0x80000000) | (exp << 16);
        *pwrd2 = wrd2;
        *pwrd3 = wrd3;
    }
}

bool fpu_get_constant(fptype *fp, int cr)
{
    uae_u32 *f = NULL;
    
    switch (cr & 0x7f)
    {
        case 0x00:
            f = xhex_pi;
            break;
        case 0x0b:
            f = xhex_l10_2;
            break;
        case 0x0c:
            f = xhex_exp_1;
            break;
        case 0x0d:
            f = xhex_l2_e;
            break;
        case 0x0e:
            f = xhex_l10_e;
            break;
        case 0x0f:
            f = xhex_zero;
            break;
        case 0x30:
            f = xhex_ln_2;
            break;
        case 0x31:
            f = xhex_ln_10;
            break;
        case 0x32:
            f = xhex_1e0;
            break;
        case 0x33:
            f = xhex_1e1;
            break;
        case 0x34:
            f = xhex_1e2;
            break;
        case 0x35:
            f = xhex_1e4;
            break;
        case 0x36:
            f = xhex_1e8;
            break;
        case 0x37:
            f = xhex_1e16;
            break;
        case 0x38:
            f = xhex_1e32;
            break;
        case 0x39:
            f = xhex_1e64;
            break;
        case 0x3a:
            f = xhex_1e128;
            break;
        case 0x3b:
            f = xhex_1e256;
            break;
        case 0x3c:
            f = xhex_1e512;
            break;
        case 0x3d:
            f = xhex_1e1024;
            break;
        case 0x3e:
            f = xhex_1e2048;
            break;
        case 0x3f:
            f = xhex_1e4096;
            break;
        default:
            return false;
    }

    to_exten_fmovem(fp, f[0], f[1], f[2]);

    return true;
}

/*
 static void fpu_format_error (void)
 {
	uaecptr newpc;
	regs.t0 = regs.t1 = 0;
	MakeSR ();
	if (!regs.s) {
 regs.usp = m68k_areg (regs, 7);
 m68k_areg (regs, 7) = regs.isp;
	}
	regs.s = 1;
	m68k_areg (regs, 7) -= 2;
	x_put_long (m68k_areg (regs, 7), 0x0000 + 14 * 4);
	m68k_areg (regs, 7) -= 4;
	x_put_long (m68k_areg (regs, 7), m68k_getpc ());
	m68k_areg (regs, 7) -= 2;
	x_put_long (m68k_areg (regs, 7), regs.sr);
	newpc = x_get_long (regs.vbr + 14 * 4);
	m68k_setpc (newpc);
 #ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
 #endif
	regs.fp_exception = true;
 }
 */

#define FPU_EXP_UNIMP_INS 0
#define FPU_EXP_DISABLED 1
#define FPU_EXP_UNIMP_DATATYPE_PRE 2
#define FPU_EXP_UNIMP_DATATYPE_POST 3
#define FPU_EXP_UNIMP_DATATYPE_PACKED_PRE 4
#define FPU_EXP_UNIMP_DATATYPE_PACKED_POST 5
#define FPU_EXP_UNIMP_EA 6


static void fpu_op_unimp (uae_u16 opcode, uae_u16 extra, uae_u32 ea, uaecptr oldpc, int type, fptype *src, int reg, int size)
{
    /* 68040 unimplemented/68060 FPU disabled exception.
     * Line F exception with different stack frame.. */
    int vector = 11;
    uaecptr newpc = m68k_getpc (); // next instruction
    static int warned = 20;
    
    regs.t0 = regs.t1 = 0;
    MakeSR ();
    if (!regs.s) {
        regs.usp = m68k_areg (regs, 7);
        if (currprefs.cpu_model == 68060) {
            m68k_areg (regs, 7) = regs.isp;
        } else if (currprefs.cpu_model >= 68020) {
            m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
        } else {
            m68k_areg (regs, 7) = regs.isp;
        }
        regs.s = 1;
        if (currprefs.mmu_model)
            mmu_set_super (regs.s != 0);
    }
    regs.fpu_exp_state = 1;
    if (currprefs.cpu_model == 68060) {
        regs.fpiar = oldpc;
        regs.exp_extra = extra;
        regs.exp_opcode = opcode;
        regs.exp_size = size;
        if (src)
            regs.exp_src1 = *src;
        regs.exp_type = type;
        if (type == FPU_EXP_DISABLED) {
            // current PC
            newpc = oldpc;
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), oldpc);
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), ea);
            m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), 0x4000 + vector * 4);
        } else if (type == FPU_EXP_UNIMP_INS) {
            // PC = next instruction
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), ea);
            m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), 0x2000 + vector * 4);
        } else if (type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE || type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST || type == FPU_EXP_UNIMP_DATATYPE_PRE || type == FPU_EXP_UNIMP_DATATYPE_POST) {
            regs.fpu_exp_state = 2; // EXC frame
            // PC = next instruction
            vector = 55;
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), ea);
            m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), 0x2000 + vector * 4);
        } else { // FPU_EXP_UNIMP_EA
            // current PC
            newpc = oldpc;
            vector = 60;
            m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), 0x0000 + vector * 4);
        }
    } else if (currprefs.cpu_model == 68040) {
        regs.fpiar = oldpc;
        regs.exp_extra = extra;
        regs.exp_opcode = opcode;
        regs.exp_size = size;
        if (src)
            regs.exp_src1 = *src;
        regs.exp_type = type;
        if (reg >= 0)
            regs.exp_src2 = regs.fp[reg];
        else
            fpclear (&regs.exp_src2);
        if (type == FPU_EXP_UNIMP_INS || type == FPU_EXP_DISABLED) {
            // PC = next instruction
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), ea);
            m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), 0x2000 + vector * 4);
        } else if (type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE || type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST || type == FPU_EXP_UNIMP_DATATYPE_PRE || type == FPU_EXP_UNIMP_DATATYPE_POST) {
            // PC = next instruction
            vector = 55;
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), ea);
            m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), 0x2000 + vector * 4);
            regs.fpu_exp_state = 2; // BUSY frame
        }
    }
    oldpc = newpc;
    m68k_areg (regs, 7) -= 4;
    x_put_long (m68k_areg (regs, 7), newpc);
    m68k_areg (regs, 7) -= 2;
    x_put_word (m68k_areg (regs, 7), regs.sr);
    newpc = x_get_long (regs.vbr + vector * 4);
    if (warned > 0) {
        write_log (_T("FPU EXCEPTION %d OP=%04X-%04X EA=%08X PC=%08X -> %08X\n"), type, opcode, extra, ea, oldpc, newpc);
#if EXCEPTION_FPP == 0
        warned--;
#endif
    }
    regs.fp_exception = true;
    m68k_setpc (newpc);
#ifdef JIT
    set_special (SPCFLAG_END_COMPILE);
#endif
}

static void fpu_op_illg2 (uae_u16 opcode, uae_u16 extra, uae_u32 ea, uaecptr oldpc)
{
    if ((currprefs.cpu_model == 68060 && (currprefs.fpu_model == 0 || (regs.pcr & 2)))
        || (currprefs.cpu_model == 68040 && currprefs.fpu_model == 0)) {
        fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_DISABLED, NULL, -1, -1);
        return;
    }
    regs.fp_exception = true;
    m68k_setpc (oldpc);
    op_illg (opcode);
}

static void fpu_op_illg (uae_u16 opcode, uae_u16 extra, uaecptr oldpc)
{
    fpu_op_illg2 (opcode, extra, 0, oldpc);
}


static void fpu_noinst (uae_u16 opcode, uaecptr pc)
{
#if EXCEPTION_FPP
    write_log (_T("Unknown FPU instruction %04X %08X\n"), opcode, pc);
#endif
    regs.fp_exception = true;
    m68k_setpc (pc);
    op_illg (opcode);
}

static bool fault_if_no_fpu (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc)
{
    if ((regs.pcr & 2) || currprefs.fpu_model <= 0) {
#if EXCEPTION_FPP
        write_log (_T("no FPU: %04X-%04X PC=%08X\n"), opcode, extra, oldpc);
#endif
        fpu_op_illg2 (opcode, extra, ea, oldpc);
        return true;
    }
    return false;
}

static bool fault_if_unimplemented_680x0 (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, fptype *src, int reg)
{
    if (fault_if_no_fpu (opcode, extra, ea, oldpc))
        return true;
    if (currprefs.cpu_model >= 68040 && currprefs.fpu_model) {
        if ((extra & (0x8000 | 0x2000)) != 0)
            return false;
        if ((extra & 0xfc00) == 0x5c00) {
            // FMOVECR
            fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_UNIMP_INS, src, reg, -1);
            return true;
        }
        uae_u16 v = extra & 0x7f;
        switch (v)
        {
            case 0x01: /* FINT */
            case 0x03: /* FINTRZ */
                // Unimplemented only in 68040.
                if (currprefs.cpu_model == 68040) {
                    fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_UNIMP_INS, src, reg, -1);
                    return true;
                }
                return false;
            case 0x02: /* FSINH */
            case 0x06: /* FLOGNP1 */
            case 0x08: /* FETOXM1 */
            case 0x09: /* FTANH */
            case 0x0a: /* FATAN */
            case 0x0c: /* FASIN */
            case 0x0d: /* FATANH */
            case 0x0e: /* FSIN */
            case 0x0f: /* FTAN */
            case 0x10: /* FETOX */
            case 0x11: /* FTWOTOX */
            case 0x12: /* FTENTOX */
            case 0x14: /* FLOGN */
            case 0x15: /* FLOG10 */
            case 0x16: /* FLOG2 */
            case 0x19: /* FCOSH */
            case 0x1c: /* FACOS */
            case 0x1d: /* FCOS */
            case 0x1e: /* FGETEXP */
            case 0x1f: /* FGETMAN */
            case 0x30: /* FSINCOS */
            case 0x31: /* FSINCOS */
            case 0x32: /* FSINCOS */
            case 0x33: /* FSINCOS */
            case 0x34: /* FSINCOS */
            case 0x35: /* FSINCOS */
            case 0x36: /* FSINCOS */
            case 0x37: /* FSINCOS */
            case 0x21: /* FMOD */
            case 0x25: /* FREM */
            case 0x26: /* FSCALE */
                fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_UNIMP_INS, src, reg, -1);
                return true;
        }
    }
    return false;
}

static bool fault_if_unimplemented_6888x (uae_u16 opcode, uae_u16 extra, uaecptr oldpc)
{
    if ((currprefs.fpu_model == 68881 || currprefs.fpu_model == 68882)) {
        uae_u16 v = extra & 0x7f;
        /* 68040/68060 only variants. 6888x = F-line exception. */
        switch (v)
        {
            case 0x62: /* FSADD */
            case 0x66: /* FDADD */
            case 0x68: /* FSSUB */
            case 0x6c: /* FDSUB */
            case 0x5a: /* FSNEG */
            case 0x5e: /* FDNEG */
            case 0x58: /* FSABS */
            case 0x5c: /* FDABS */
            case 0x63: /* FSMUL */
            case 0x67: /* FDMUL */
            case 0x41: /* FSSQRT */
            case 0x45: /* FDSQRT */
                fpu_noinst (opcode, oldpc);
                return true;
        }
    }
    return false;
}

static bool fault_if_60 (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, int type)
{
    if (currprefs.cpu_model == 68060 && currprefs.fpu_model) {
        fpu_op_unimp (opcode, extra, ea, oldpc, type, NULL, -1, -1);
        return true;
    }
    return false;
}

static bool fault_if_4060 (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, int type, fptype *src, uae_u32 *pack)
{
    if (currprefs.cpu_model >= 68040 && currprefs.fpu_model) {
        if (pack) {
            regs.exp_pack[0] = pack[0];
            regs.exp_pack[1] = pack[1];
            regs.exp_pack[2] = pack[2];
        }
        fpu_op_unimp (opcode, extra, ea, oldpc, type, src, -1, -1);
        return true;
    }
    return false;
}

static bool fault_if_no_fpu_u (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc)
{
    if (fault_if_no_fpu (opcode, extra, ea, oldpc))
        return true;
    if (currprefs.cpu_model == 68060 && currprefs.fpu_model) {
        // 68060 FTRAP, FDBcc or FScc are not implemented.
        fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_UNIMP_INS, NULL, -1, -1);
        return true;
    }
    return false;
}

static bool fault_if_no_6888x (uae_u16 opcode, uae_u16 extra, uaecptr oldpc)
{
    if (currprefs.cpu_model < 68040 && currprefs.fpu_model <= 0) {
#if EXCEPTION_FPP
        write_log (_T("6888x no FPU: %04X-%04X PC=%08X\n"), opcode, extra, oldpc);
#endif
        m68k_setpc (oldpc);
        regs.fp_exception = true;
        op_illg (opcode);
        return true;
    }
    return false;
}


static int get_fpu_version (void)
{
    int v = 0;
    
    switch (currprefs.fpu_model)
    {
        case 68881:
        case 68882:
            v = 0x1f;
            break;
        case 68040:
            if (currprefs.fpu_revision == 0x40)
                v = 0x40;
            else
                v = 0x41;
            break;
    }
    return v;
}

static void fpu_null (void)
{
    int i;
    regs.fpu_state = 0;
    regs.fpu_exp_state = 0;
    regs.fpcr = 0;
    regs.fpsr = 0;
    regs.fpiar = 0;
    fpset(&regs.fp_result, 1);
    fpclear (&regs.fp_result);
    for (i = 0; i < 8; i++)
        fpnan (&regs.fp[i]);
}

void fpsr_set_exception(uae_u32 exception)
{
    regs.fpsr |= exception;
}
void fpsr_check_exception(void)
{
    // Any exception status bit and matching exception enable bits set?
    uae_u32 exception = (regs.fpsr >> 8) & (regs.fpcr >> 8);
    
    if (exception) {
        int vector = 0;
        int vtable[8] = { 49, 49, 50, 51, 53, 52, 54, 48 };
        int i;
        for (i = 7; i >= 0; i--) {
            if (exception & (1 << i)) {
                vector = vtable[i];
                break;
            }
        }
        // logging only so far
        write_log (_T("FPU exception: FPSR: %08x, FPCR: %04x (vector: %d)!\n"), regs.fpsr, regs.fpcr, vector);
    }
}
void fpsr_set_result(fptype result)
{
    regs.fp_result = result;
    
    // condition code byte
    regs.fpsr &= 0x00fffff8; // clear cc
    if (fp_is_nan (&regs.fp_result)) {
        regs.fpsr |= FPSR_CC_NAN;
    } else {
        if (fp_is_zero(&regs.fp_result))
            regs.fpsr |= FPSR_CC_Z;
        if (fp_is_infinity (&regs.fp_result))
            regs.fpsr |= FPSR_CC_I;
    }
    if (fp_is_neg(&regs.fp_result))
        regs.fpsr |= FPSR_CC_N;
    
    // check if result is signaling nan
    if (fp_is_snan(&regs.fp_result))
        regs.fpsr |= FPSR_SNAN;
}
void fpsr_clear_status(void)
{
    // clear exception status byte only
    regs.fpsr &= 0x0fff00f8;
    
    // clear external status
    clear_fp_status();
}
void fpsr_make_status(void)
{
    // get external status
    get_fp_status(&regs.fpsr);
    
    // update accrued exception byte
    if (regs.fpsr & (FPSR_BSUN | FPSR_SNAN | FPSR_OPERR))
        regs.fpsr |= FPSR_AE_IOP;  // IOP = BSUN || SNAN || OPERR
    if (regs.fpsr & FPSR_OVFL)
        regs.fpsr |= FPSR_AE_OVFL; // OVFL = OVFL
    if ((regs.fpsr & FPSR_UNFL) && (regs.fpsr & FPSR_INEX2))
        regs.fpsr |= FPSR_AE_UNFL; // UNFL = UNFL && INEX2
    if (regs.fpsr & FPSR_DZ)
        regs.fpsr |= FPSR_AE_DZ;   // DZ = DZ
    if (regs.fpsr & (FPSR_OVFL | FPSR_INEX2 | FPSR_INEX1))
        regs.fpsr |= FPSR_AE_INEX; // INEX = INEX1 || INEX2 || OVFL
    
    fpsr_check_exception();
}
int fpsr_set_bsun(void)
{
    regs.fpsr |= FPSR_BSUN;
    regs.fpsr |= FPSR_AE_IOP;
    
    if (regs.fpcr & FPSR_BSUN) {
        // logging only so far
        write_log (_T("FPU exception: BSUN! (FPSR: %08x, FPCR: %04x)\n"), regs.fpsr, regs.fpcr);
        return 0; // return 1, once BSUN exception works
    }
    return 0;
}

uae_u32 fpp_get_fpsr (void)
{
    return regs.fpsr;
}

void fpp_set_fpsr (uae_u32 val)
{
    regs.fpsr = val;
}

void fpp_set_fpcr (uae_u32 val)
{
    set_fp_mode(val);
    regs.fpcr = val & 0xffff;
}

static uae_u32 get_ftag (uae_u32 w1, uae_u32 w2, uae_u32 w3, int size)
{
    int exp = (w1 >> 16) & 0x7fff;
    
    if (exp == 0) {
        if (!w2 && !w3)
            return 1; // ZERO
        if (size == 0 || size == 1)
            return 5; // Single/double DENORMAL
        return 4; // Extended DENORMAL or UNNORMAL
    } else if (exp == 0x7fff)  {
        int s = w2 >> 30;
        int z = (w2 & 0x3fffffff) == 0 && w3 == 0;
        if ((s == 0 && !z) || (s == 2 && !z))
            return 2; // INF
        return 3; // NAN
    } else {
        if (!(w2 & 0x80000000))
            return 4; // Extended UNNORMAL
        return 0; // NORMAL
    }
}

/* single   : S  8*E 23*F */
/* double   : S 11*E 52*F */
/* extended : S 15*E 64*F */
/* E = 0 & F = 0 -> 0 */
/* E = MAX & F = 0 -> Infin */
/* E = MAX & F # 0 -> NotANumber */
/* E = biased by 127 (single) ,1023 (double) ,16383 (extended) */

static void to_pack (fptype *fp, uae_u32 *wrd)
{
    long double d;
    char *cp;
    char str[100];
    
    if (((wrd[0] >> 16) & 0x7fff) == 0x7fff) {
        // infinity has extended exponent and all 0 packed fraction
        // nans are copies bit by bit
        to_exten_fmovem(fp, wrd[0], wrd[1], wrd[2]);
        return;
    }
    if (!(wrd[0] & 0xf) && !wrd[1] && !wrd[2]) {
        // exponent is not cared about, if mantissa is zero
        wrd[0] &= 0x80000000;
        to_exten_fmovem(fp, wrd[0], wrd[1], wrd[2]);
        return;
    }
    
    cp = str;
    if (wrd[0] & 0x80000000)
        *cp++ = '-';
    *cp++ = (wrd[0] & 0xf) + '0';
    *cp++ = '.';
    *cp++ = ((wrd[1] >> 28) & 0xf) + '0';
    *cp++ = ((wrd[1] >> 24) & 0xf) + '0';
    *cp++ = ((wrd[1] >> 20) & 0xf) + '0';
    *cp++ = ((wrd[1] >> 16) & 0xf) + '0';
    *cp++ = ((wrd[1] >> 12) & 0xf) + '0';
    *cp++ = ((wrd[1] >> 8) & 0xf) + '0';
    *cp++ = ((wrd[1] >> 4) & 0xf) + '0';
    *cp++ = ((wrd[1] >> 0) & 0xf) + '0';
    *cp++ = ((wrd[2] >> 28) & 0xf) + '0';
    *cp++ = ((wrd[2] >> 24) & 0xf) + '0';
    *cp++ = ((wrd[2] >> 20) & 0xf) + '0';
    *cp++ = ((wrd[2] >> 16) & 0xf) + '0';
    *cp++ = ((wrd[2] >> 12) & 0xf) + '0';
    *cp++ = ((wrd[2] >> 8) & 0xf) + '0';
    *cp++ = ((wrd[2] >> 4) & 0xf) + '0';
    *cp++ = ((wrd[2] >> 0) & 0xf) + '0';
    *cp++ = 'E';
    if (wrd[0] & 0x40000000)
        *cp++ = '-';
    *cp++ = ((wrd[0] >> 24) & 0xf) + '0';
    *cp++ = ((wrd[0] >> 20) & 0xf) + '0';
    *cp++ = ((wrd[0] >> 16) & 0xf) + '0';
    *cp = 0;
#if USE_LONG_DOUBLE
    sscanf (str, "%Le", &d);
#else
    sscanf (str, "%le", &d);
#endif
    from_native(d, fp);
}

static void from_pack (fptype *src, uae_u32 *wrd, int kfactor)
{
    int i, j, t;
    int exp;
    int ndigits;
    char *cp, *strp;
    char str[100];
    long double fp;
    
    if (fp_is_nan (src)) {
        // copied bit by bit, no conversion
        from_exten(src, &wrd[0], &wrd[1], &wrd[2]);
        return;
    }
    if (fp_is_infinity (src)) {
        // extended exponent and all 0 packed fraction
        from_exten(src, &wrd[0], &wrd[1], &wrd[2]);
        wrd[1] = wrd[2] = 0;
        return;
    }
    
    to_native(&fp, *src);
    
    wrd[0] = wrd[1] = wrd[2] = 0;
    
#if USE_LONG_DOUBLE
    sprintf (str, "%#.17Le", fp);
#else
    sprintf (str, "%#.17e", fp);
#endif
    
    // get exponent
    cp = str;
    while (*cp++ != 'e');
    if (*cp == '+')
        cp++;
    exp = atoi (cp);
    
    // remove trailing zeros
    cp = str;
    while (*cp != 'e')
        cp++;
    cp[0] = 0;
    cp--;
    while (cp > str && *cp == '0') {
        *cp = 0;
        cp--;
    }
    
    cp = str;
    // get sign
    if (*cp == '-') {
        cp++;
        wrd[0] = 0x80000000;
    } else if (*cp == '+') {
        cp++;
    }
    strp = cp;
    
    if (kfactor <= 0) {
        ndigits = abs (exp) + (-kfactor) + 1;
    } else {
        if (kfactor > 17) {
            kfactor = 17;
            fpsr_set_exception(FPSR_OPERR);
        }
        ndigits = kfactor;
    }
    
    if (ndigits < 0)
        ndigits = 0;
    if (ndigits > 16)
        ndigits = 16;
    
    // remove decimal point
    strp[1] = strp[0];
    strp++;
    // add trailing zeros
    i = strlen (strp);
    cp = strp + i;
    while (i < ndigits) {
        *cp++ = '0';
        i++;
    }
    i = ndigits + 1;
    while (i < 17) {
        strp[i] = 0;
        i++;
    }
    *cp = 0;
    i = ndigits - 1;
    // need to round?
    if (i >= 0 && strp[i + 1] >= '5') {
        while (i >= 0) {
            strp[i]++;
            if (strp[i] <= '9')
                break;
            if (i == 0) {
                strp[i] = '1';
                exp++;
            } else {
                strp[i] = '0';
            }
            i--;
        }
    }
    strp[ndigits] = 0;
    
    // store first digit of mantissa
    cp = strp;
    wrd[0] |= *cp++ - '0';
    
    // store rest of mantissa
    for (j = 1; j < 3; j++) {
        for (i = 0; i < 8; i++) {
            wrd[j] <<= 4;
            if (*cp >= '0' && *cp <= '9')
                wrd[j] |= *cp++ - '0';
        }
    }
    
    // exponent
    if (exp < 0) {
        wrd[0] |= 0x40000000;
        exp = -exp;
    }
    if (exp > 9999) // ??
        exp = 9999;
    if (exp > 999) {
        int d = exp / 1000;
        wrd[0] |= d << 12;
        exp -= d * 1000;
        fpsr_set_exception(FPSR_OPERR);
    }
    i = 100;
    t = 0;
    while (i >= 1) {
        int d = exp / i;
        t <<= 4;
        t |= d;
        exp -= d * i;
        i /= 10;
    }
    wrd[0] |= t << 16;
}

// 68040/060 does not support denormals
static bool fault_if_no_denormal_support_pre(uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, fptype *fpd, int size)
{
    if (currprefs.cpu_model >= 68040 && currprefs.fpu_model) {
        if (fp_is_unnormal(fpd) || fp_is_denormal(fpd)) {
            fpu_op_unimp(opcode, extra, ea, oldpc, FPU_EXP_UNIMP_DATATYPE_PRE, fpd, -1, size);
            return true;
        }
    }

    return false;
}
static bool fault_if_no_denormal_support_post(uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, fptype *fpd, int size)
{
    if (currprefs.cpu_model >= 68040 && currprefs.fpu_model) {
        if (fp_is_unnormal(fpd) || fp_is_denormal(fpd)) {
            fpu_op_unimp(opcode, extra, ea, oldpc, FPU_EXP_UNIMP_DATATYPE_POST, fpd, -1, size);
            return true;
        }
    }

    return false;
}

static int get_fp_value (uae_u32 opcode, uae_u16 extra, fptype *src, uaecptr oldpc, uae_u32 *adp)
{
    int size, mode, reg;
    uae_u32 ad = 0;
    static const int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
    static const int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };
    uae_u32 exts[3];
    int doext = 0;
    
    if (!(extra & 0x4000)) {
        if (fault_if_no_fpu (opcode, extra, 0, oldpc))
            return -1;
        *src = regs.fp[(extra >> 10) & 7];
        if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 2))
            return -1;
        return 1;
    }
    mode = (opcode >> 3) & 7;
    reg = opcode & 7;
    size = (extra >> 10) & 7;
    
    switch (mode) {
        case 0:
            switch (size)
        {
            case 6:
                fpset(src, (uae_s8) m68k_dreg (regs, reg));
                break;
            case 4:
                fpset(src, (uae_s16) m68k_dreg (regs, reg));
                break;
            case 0:
                fpset(src, (uae_s32) m68k_dreg (regs, reg));
                break;
            case 1:
                to_single (src, m68k_dreg (regs, reg));
                if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 0))
                    return -1;
                break;
            default:
                return 0;
        }
            return 1;
        case 1:
            return 0;
        case 2:
            ad = m68k_areg (regs, reg);
            break;
        case 3:
            if (currprefs.mmu_model) {
                mmufixup[0].reg = reg;
                mmufixup[0].value = m68k_areg (regs, reg);
                fpu_mmu_fixup = true;
            }
            ad = m68k_areg (regs, reg);
            m68k_areg (regs, reg) += reg == 7 ? sz2[size] : sz1[size];
            break;
        case 4:
            if (currprefs.mmu_model) {
                mmufixup[0].reg = reg;
                mmufixup[0].value = m68k_areg (regs, reg);
                fpu_mmu_fixup = true;
            }
            m68k_areg (regs, reg) -= reg == 7 ? sz2[size] : sz1[size];
            ad = m68k_areg (regs, reg);
            break;
        case 5:
            ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) x_cp_next_iword ();
            break;
        case 6:
            ad = x_cp_get_disp_ea_020 (m68k_areg (regs, reg), 0);
            break;
        case 7:
            switch (reg)
        {
            case 0: // (xxx).W
                ad = (uae_s32) (uae_s16) x_cp_next_iword ();
                break;
            case 1: // (xxx).L
                ad = x_cp_next_ilong ();
                break;
            case 2: // (d16,PC)
                ad = m68k_getpc ();
                ad += (uae_s32) (uae_s16) x_cp_next_iword ();
                break;
            case 3: // (d8,PC,Xn)+
                ad = x_cp_get_disp_ea_020 (m68k_getpc (), 0);
                break;
            case 4: // #imm
                doext = 1;
                switch (size)
            {
                case 0: // L
                case 1: // S
                    exts[0] = x_cp_next_ilong ();
                    break;
                case 2: // X
                case 3: // P
                    // 68060 and immediate X or P: unimplemented effective address
                    if (fault_if_60 (opcode, extra, ad, oldpc, FPU_EXP_UNIMP_EA))
                        return -1;
                    exts[0] = x_cp_next_ilong ();
                    exts[1] = x_cp_next_ilong ();
                    exts[2] = x_cp_next_ilong ();
                    break;
                case 4: // W
                    exts[0] = x_cp_next_iword ();
                    break;
                case 5: // D
                    exts[0] = x_cp_next_ilong ();
                    exts[1] = x_cp_next_ilong ();
                    break;
                case 6: // B
                    exts[0] = x_cp_next_iword ();
                    break;
            }
                break;
            default:
                return 0;
        }
    }
    
    *adp = ad;
    
    if (currprefs.fpu_model == 68060 && fault_if_unimplemented_680x0 (opcode, extra, ad, oldpc, src, -1))
        return -1;
    
    switch (size)
    {
        case 0:
            fpset(src, (uae_s32) (doext ? exts[0] : x_cp_get_long (ad)));
            break;
        case 1:
            to_single (src, (doext ? exts[0] : x_cp_get_long (ad)));
            if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 0))
                return -1;
            break;
        case 2:
        {
            uae_u32 wrd1, wrd2, wrd3;
            wrd1 = (doext ? exts[0] : x_cp_get_long (ad));
            ad += 4;
            wrd2 = (doext ? exts[1] : x_cp_get_long (ad));
            ad += 4;
            wrd3 = (doext ? exts[2] : x_cp_get_long (ad));
            to_exten (src, wrd1, wrd2, wrd3);
            if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 2))
                return -1;
        }
            break;
        case 3:
        {
            uae_u32 wrd[3];
            uae_u32 adold = ad;
            if (currprefs.cpu_model == 68060) {
                if (fault_if_4060 (opcode, extra, adold, oldpc, FPU_EXP_UNIMP_DATATYPE_PACKED_PRE, NULL, wrd))
                    return -1;
            }
            wrd[0] = (doext ? exts[0] : x_cp_get_long (ad));
            ad += 4;
            wrd[1] = (doext ? exts[1] : x_cp_get_long (ad));
            ad += 4;
            wrd[2] = (doext ? exts[2] : x_cp_get_long (ad));
            if (fault_if_4060 (opcode, extra, adold, oldpc, FPU_EXP_UNIMP_DATATYPE_PACKED_PRE, NULL, wrd))
                return -1;
            to_pack (src, wrd);
            return 1;
        }
            break;
        case 4:
            fpset(src, (uae_s16) (doext ? exts[0] : x_cp_get_word (ad)));
            break;
        case 5:
        {
            uae_u32 wrd1, wrd2;
            wrd1 = (doext ? exts[0] : x_cp_get_long (ad));
            ad += 4;
            wrd2 = (doext ? exts[1] : x_cp_get_long (ad));
            to_double (src, wrd1, wrd2);
            if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 1))
                return -1;
        }
            break;
        case 6:
            fpset(src, (uae_s8) (doext ? exts[0] : x_cp_get_byte (ad)));
            break;
        default:
            return 0;
    }
    return 1;
}

static int put_fp_value (fptype *value, uae_u32 opcode, uae_u16 extra, uaecptr oldpc)
{
    int size, mode, reg;
    uae_u32 ad = 0;
    static int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
    static int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };
    
#if DEBUG_FPP
    write_log (_T("PUTFP: %f %04X %04X\n"), value, opcode, extra);
#endif
    if (!(extra & 0x4000)) {
        if (fault_if_no_fpu (opcode, extra, 0, oldpc))
            return 1;
        regs.fp[(extra >> 10) & 7] = *value;
        return 1;
    }
    reg = opcode & 7;
    mode = (opcode >> 3) & 7;
    size = (extra >> 10) & 7;
    ad = -1;
    switch (mode)
    {
        case 0:
            switch (size)
        {
            case 6:
                m68k_dreg (regs, reg) = (uae_u32)(((to_int (value, 0) & 0xff)
                                                   | (m68k_dreg (regs, reg) & ~0xff)));
                break;
            case 4:
                m68k_dreg (regs, reg) = (uae_u32)(((to_int (value, 1) & 0xffff)
                                                   | (m68k_dreg (regs, reg) & ~0xffff)));
                break;
            case 0:
                m68k_dreg (regs, reg) = (uae_u32)to_int (value, 2);
                break;
            case 1:
                m68k_dreg (regs, reg) = from_single (value);
                break;
            default:
                return 0;
        }
            return 1;
        case 1:
            return 0;
        case 2:
            ad = m68k_areg (regs, reg);
            break;
        case 3:
            if (currprefs.mmu_model) {
                mmufixup[0].reg = reg;
                mmufixup[0].value = m68k_areg (regs, reg);
                fpu_mmu_fixup = true;
            }
            ad = m68k_areg (regs, reg);
            m68k_areg (regs, reg) += reg == 7 ? sz2[size] : sz1[size];
            break;
        case 4:
            if (currprefs.mmu_model) {
                mmufixup[0].reg = reg;
                mmufixup[0].value = m68k_areg (regs, reg);
                fpu_mmu_fixup = true;
            }
            m68k_areg (regs, reg) -= reg == 7 ? sz2[size] : sz1[size];
            ad = m68k_areg (regs, reg);
            break;
        case 5:
            ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) x_cp_next_iword ();
            break;
        case 6:
            ad = x_cp_get_disp_ea_020 (m68k_areg (regs, reg), 0);
            break;
        case 7:
            switch (reg)
        {
            case 0:
                ad = (uae_s32) (uae_s16) x_cp_next_iword ();
                break;
            case 1:
                ad = x_cp_next_ilong ();
                break;
            case 2:
                ad = m68k_getpc ();
                ad += (uae_s32) (uae_s16) x_cp_next_iword ();
                break;
            case 3:
                ad = x_cp_get_disp_ea_020 (m68k_getpc (), 0);
                break;
            default:
                return 0;
        }
    }
    
    if (fault_if_no_fpu (opcode, extra, ad, oldpc))
        return 1;
    
    switch (size)
    {
        case 0:
            if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
                return 1;
            x_cp_put_long(ad, (uae_u32)to_int(value, 2));
            break;
        case 1:
            if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
                return -1;
            x_cp_put_long(ad, from_single(value));
            break;
        case 2:
        {
            uae_u32 wrd1, wrd2, wrd3;
            if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
                return 1;
            from_exten(value, &wrd1, &wrd2, &wrd3);
            x_cp_put_long (ad, wrd1);
            ad += 4;
            x_cp_put_long (ad, wrd2);
            ad += 4;
            x_cp_put_long (ad, wrd3);
        }
            break;
        case 3: // Packed-Decimal Real with Static k-Factor
        case 7: // Packed-Decimal Real with Dynamic k-Factor (P{Dn}) (reg to memory only)
        {
            uae_u32 wrd[3];
            int kfactor;
            if (fault_if_4060 (opcode, extra, ad, oldpc, FPU_EXP_UNIMP_DATATYPE_PACKED_POST, value, NULL))
                return 1;
            kfactor = size == 7 ? m68k_dreg (regs, (extra >> 4) & 7) : extra;
            kfactor &= 127;
            if (kfactor & 64)
                kfactor |= ~63;
            from_pack (value, wrd, kfactor);
            x_cp_put_long (ad, wrd[0]);
            ad += 4;
            x_cp_put_long (ad, wrd[1]);
            ad += 4;
            x_cp_put_long (ad, wrd[2]);
        }
            break;
        case 4:
            if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
                return 1;
            x_cp_put_word(ad, (uae_s16)to_int(value, 1));
            break;
        case 5:
        {
            uae_u32 wrd1, wrd2;
            if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 1))
                return -1;
            from_double(value, &wrd1, &wrd2);
            x_cp_put_long (ad, wrd1);
            ad += 4;
            x_cp_put_long (ad, wrd2);
        }
            break;
        case 6:
            if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
                return 1;
            x_cp_put_byte(ad, (uae_s8)to_int(value, 0));
            break;
        default:
            return 0;
    }
    return 1;
}

STATIC_INLINE int get_fp_ad (uae_u32 opcode, uae_u32 * ad)
{
    int mode;
    int reg;
    
    mode = (opcode >> 3) & 7;
    reg = opcode & 7;
    switch (mode)
    {
        case 0:
        case 1:
            return 0;
        case 2:
            *ad = m68k_areg (regs, reg);
            break;
        case 3:
            *ad = m68k_areg (regs, reg);
            break;
        case 4:
            *ad = m68k_areg (regs, reg);
            break;
        case 5:
            *ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) x_cp_next_iword ();
            break;
        case 6:
            *ad = x_cp_get_disp_ea_020 (m68k_areg (regs, reg), 0);
            break;
        case 7:
            switch (reg)
        {
            case 0:
                *ad = (uae_s32) (uae_s16) x_cp_next_iword ();
                break;
            case 1:
                *ad = x_cp_next_ilong ();
                break;
            case 2:
                *ad = m68k_getpc ();
                *ad += (uae_s32) (uae_s16) x_cp_next_iword ();
                break;
            case 3:
                *ad = x_cp_get_disp_ea_020 (m68k_getpc (), 0);
                break;
            default:
                return 0;
        }
    }
    return 1;
}

int fpp_cond (int condition)
{
    int NotANumber, Z, N;
    
    NotANumber = (regs.fpsr & FPSR_CC_NAN) ? 1 : 0;
    N = (regs.fpsr & FPSR_CC_N) ? 1 : 0;
    Z = (regs.fpsr & FPSR_CC_Z) ? 1 : 0;
    
    if ((condition & 0x10) && NotANumber) {
        if (fpsr_set_bsun())
            return -2;
    }
    
    switch (condition)
    {
        case 0x00:
            return 0;
        case 0x01:
            return Z;
        case 0x02:
            return !(NotANumber || Z || N);
        case 0x03:
            return Z || !(NotANumber || N);
        case 0x04:
            return N && !(NotANumber || Z);
        case 0x05:
            return Z || (N && !NotANumber);
        case 0x06:
            return !(NotANumber || Z);
        case 0x07:
            return !NotANumber;
        case 0x08:
            return NotANumber;
        case 0x09:
            return NotANumber || Z;
        case 0x0a:
            return NotANumber || !(N || Z);
        case 0x0b:
            return NotANumber || Z || !N;
        case 0x0c:
            return NotANumber || (N && !Z);
        case 0x0d:
            return NotANumber || Z || N;
        case 0x0e:
            return !Z;
        case 0x0f:
            return 1;
        case 0x10:
            return 0;
        case 0x11:
            return Z;
        case 0x12:
            return !(NotANumber || Z || N);
        case 0x13:
            return Z || !(NotANumber || N);
        case 0x14:
            return N && !(NotANumber || Z);
        case 0x15:
            return Z || (N && !NotANumber);
        case 0x16:
            return !(NotANumber || Z);
        case 0x17:
            return !NotANumber;
        case 0x18:
            return NotANumber;
        case 0x19:
            return NotANumber || Z;
        case 0x1a:
            return NotANumber || !(N || Z);
        case 0x1b:
            return NotANumber || Z || !N;
        case 0x1c:
            return NotANumber || (N && !Z);
        case 0x1d:
            return NotANumber || Z || N;
        case 0x1e:
            return !Z;
        case 0x1f:
            return 1;
    }
    return -1;
}

static void maybe_idle_state (void)
{
    // conditional floating point instruction does not change state
    // from null to idle on 68040/060.
    if (currprefs.fpu_model == 68881 || currprefs.fpu_model == 68882)
        regs.fpu_state = 1;
}

void fpuop_dbcc (uae_u32 opcode, uae_u16 extra)
{
    uaecptr pc = m68k_getpc ();
    uae_s32 disp;
    int cc;
    
    regs.fp_exception = false;
#if DEBUG_FPP
    write_log (_T("fdbcc_opp at %08x\n"), m68k_getpc ());
#endif
    if (fault_if_no_6888x (opcode, extra, pc - 4))
        return;
    
    disp = (uae_s32) (uae_s16) x_cp_next_iword ();
    if (fault_if_no_fpu_u (opcode, extra, pc + disp, pc - 4))
        return;
    regs.fpiar = pc - 4;
    maybe_idle_state ();
    cc = fpp_cond (extra & 0x3f);
    if (cc < 0) {
        if (cc == -2)
            return; // BSUN
        else
            fpu_op_illg (opcode, extra, regs.fpiar);
    } else if (!cc) {
        int reg = opcode & 0x7;
        
        m68k_dreg (regs, reg) = ((m68k_dreg (regs, reg) & 0xffff0000)
                                 | (((m68k_dreg (regs, reg) & 0xffff) - 1) & 0xffff));
        if ((m68k_dreg (regs, reg) & 0xffff) != 0xffff) {
            m68k_setpc (pc + disp);
            regs.fp_branch = true;
        }
    }
}

void fpuop_scc (uae_u32 opcode, uae_u16 extra)
{
    uae_u32 ad = 0;
    int cc;
    uaecptr pc = m68k_getpc () - 4;
    
    regs.fp_exception = false;
#if DEBUG_FPP
    write_log (_T("fscc_opp at %08x\n"), m68k_getpc ());
#endif
    
    if (fault_if_no_6888x (opcode, extra, pc))
        return;
    
    if (opcode & 0x38) {
        if (get_fp_ad (opcode, &ad) == 0) {
            fpu_noinst (opcode, regs.fpiar);
            return;
        }
    }
    
    if (fault_if_no_fpu_u (opcode, extra, ad, pc))
        return;
    
    regs.fpiar = pc;
    maybe_idle_state ();
    cc = fpp_cond (extra & 0x3f);
    if (cc < 0) {
        if (cc == -2)
            return; // BSUN
        else
            fpu_op_illg (opcode, extra, regs.fpiar);
    } else if ((opcode & 0x38) == 0) {
        m68k_dreg (regs, opcode & 7) = (m68k_dreg (regs, opcode & 7) & ~0xff) | (cc ? 0xff : 0x00);
    } else {
        x_cp_put_byte (ad, cc ? 0xff : 0x00);
    }
}

void fpuop_trapcc (uae_u32 opcode, uaecptr oldpc, uae_u16 extra)
{
    int cc;
    
    regs.fp_exception = false;
#if DEBUG_FPP
    write_log (_T("ftrapcc_opp at %08x\n"), m68k_getpc ());
#endif
    if (fault_if_no_fpu_u (opcode, extra, 0, oldpc))
        return;
    
    regs.fpiar = oldpc;
    maybe_idle_state ();
    cc = fpp_cond (extra & 0x3f);
    if (cc < 0) {
        if (cc == -2)
            return; // BSUN
        else
            fpu_op_illg (opcode, extra, regs.fpiar);
    } else if (cc) {
        Exception (7);
    }
}

void fpuop_bcc (uae_u32 opcode, uaecptr oldpc, uae_u32 extra)
{
    int cc;
    
    regs.fp_exception = false;
#if DEBUG_FPP
    write_log (_T("fbcc_opp at %08x\n"), m68k_getpc ());
#endif
    if (fault_if_no_fpu (opcode, extra, 0, oldpc - 2))
        return;
    
    regs.fpiar = oldpc - 2;
    maybe_idle_state ();
    cc = fpp_cond (opcode & 0x3f);
    if (cc < 0) {
        if (cc == -2)
            return; // BSUN
        else
            fpu_op_illg (opcode, extra, regs.fpiar);
    } else if (cc) {
        if ((opcode & 0x40) == 0)
            extra = (uae_s32) (uae_s16) extra;
        m68k_setpc (oldpc + extra);
        regs.fp_branch = true;
    }
}

void fpuop_save (uae_u32 opcode)
{
    uae_u32 ad;
    int incr = (opcode & 0x38) == 0x20 ? -1 : 1;
    int fpu_version = get_fpu_version ();
    uaecptr pc = m68k_getpc () - 2;
    int i;
    
    regs.fp_exception = false;
#if DEBUG_FPP
    write_log (_T("fsave_opp at %08x\n"), m68k_getpc ());
#endif
    
    if (fault_if_no_6888x (opcode, 0, pc))
        return;
    
    if (get_fp_ad (opcode, &ad) == 0) {
        fpu_op_illg (opcode, 0, pc);
        return;
    }
    
    if (fault_if_no_fpu (opcode, 0, ad, pc))
        return;
    
    if (currprefs.fpu_model == 68060) {
        /* 12 byte 68060 NULL/IDLE/EXCP frame.  */
        int frame_size = 12;
        uae_u32 frame_id, frame_v1, frame_v2;
        
        if (regs.fpu_exp_state > 1) {
            uae_u32 src1[3];
            from_exten (&regs.exp_src1, &src1[0], &src1[1], &src1[2]);
            frame_id = 0x0000e000 | src1[0];
            frame_v1 = src1[1];
            frame_v2 = src1[2];
            
#if EXCEPTION_FPP
            write_log(_T("68060 FSAVE EXCP %s\n"), fp_print(&regs.exp_src1));
#endif
            
        } else {
            frame_id = regs.fpu_state == 0 ? 0x00000000 : 0x00006000;
            frame_v1 = 0;
            frame_v2 = 0;
        }
        if (incr < 0)
            ad -= frame_size;
        x_put_long (ad, frame_id);
        ad += 4;
        x_put_long (ad, frame_v1);
        ad += 4;
        x_put_long (ad, frame_v2);
        ad += 4;
        if (incr < 0)
            ad -= frame_size;
    } else if (currprefs.fpu_model == 68040) {
        if (!regs.fpu_exp_state) {
            /* 4 byte 68040 NULL/IDLE frame.  */
            uae_u32 frame_id = regs.fpu_state == 0 ? 0 : fpu_version << 24;
            if (incr < 0) {
                ad -= 4;
                x_put_long (ad, frame_id);
            } else {
                x_put_long (ad, frame_id);
                ad += 4;
            }
        } else {
            /* 44 (rev $40) and 52 (rev $41) byte 68040 unimplemented instruction frame */
            /* 96 byte 68040 busy frame */
            int frame_size = regs.fpu_exp_state == 2 ? 0x64 : (fpu_version >= 0x41 ? 0x34 : 0x2c);
            uae_u32 frame_id = ((fpu_version << 8) | (frame_size - 4)) << 16;
            uae_u32 src1[3], src2[3];
            uae_u32 stag, dtag;
            uae_u32 extra = regs.exp_extra;
            
            from_exten(&regs.exp_src1, &src1[0], &src1[1], &src1[2]);
            from_exten(&regs.exp_src2, &src2[0], &src2[1], &src2[2]);
            stag = get_ftag(src1[0], src1[1], src1[2], regs.exp_size);
            dtag = get_ftag(src2[0], src2[1], src2[2], -1);
            if ((extra & 0x7f) == 4) // FSQRT 4->5
                extra |= 1;
            
#if EXCEPTION_FPP
            write_log(_T("68040 FSAVE %d (%d), CMDREG=%04X"), regs.exp_type, frame_size, extra);
            if (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE) {
                write_log(_T(" PACKED %08x-%08x-%08x"), regs.exp_pack[0], regs.exp_pack[1], regs.exp_pack[2]);
            } else if (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST) {
                write_log(_T(" SRC=%s (%08x-%08x-%08x %d)"),
                          fp_print(&regs.exp_src1), src1[0], src1[1], src1[2], stag);
                write_log(_T(" DST=%s (%08x-%08x-%08x %d)"),
                          fp_print(&regs.exp_src2), src2[0], src2[1], src2[2], dtag);
            }
            write_log(_T("\n"));
#endif
            
            if (incr < 0)
                ad -= frame_size;
            x_put_long (ad, frame_id);
            ad += 4;
            if (regs.fpu_exp_state == 2) {
                /* BUSY frame */
                x_put_long(ad, 0);
                ad += 4;
                x_put_long(ad, 0); // CU_SAVEPC (Software shouldn't care)
                ad += 4;
                x_put_long(ad, 0);
                ad += 4;
                x_put_long(ad, 0);
                ad += 4;
                x_put_long(ad, 0);
                ad += 4;
                x_put_long(ad, 0); // WBTS/WBTE (No E3 emulated yet)
                ad += 4;
                x_put_long(ad, 0); // WBTM
                ad += 4;
                x_put_long(ad, 0); // WBTM
                ad += 4;
                x_put_long(ad, 0);
                ad += 4;
                x_put_long(ad, regs.fpiar); // FPIARCU (same as FPU PC or something else?)
                ad += 4;
                x_put_long(ad, 0);
                ad += 4;
                x_put_long(ad, 0);
                ad += 4;
            }
            if (fpu_version >= 0x41 || regs.fpu_exp_state == 2) {
                x_put_long (ad, ((extra & (0x200 | 0x100 | 0x80)) | (extra & (0x40 | 0x02 | 0x01)) | ((extra >> 1) & (0x04 | 0x08 | 0x10)) | ((extra & 0x04) ? 0x20 : 0x00)) << 16); // CMDREG3B
                ad += 4;
                x_put_long (ad, 0);
                ad += 4;
            }
            x_put_long (ad, stag << 29); // STAG
            ad += 4;
            x_put_long (ad, extra << 16); // CMDREG1B
            ad += 4;
            x_put_long (ad, dtag << 29); // DTAG
            ad += 4;
            if (fpu_version >= 0x41 || regs.fpu_exp_state == 2) {
                x_put_long(ad, (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE ? 1 << 26 : 0) | (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST ? 1 << 20 : 0)); // E1 and T
                ad += 4;
            } else {
                x_put_long(ad, (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE || regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST) ? 1 << 26 : 0); // E1
                ad += 4;
            }
            if (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE) {
                x_put_long (ad, 0); // FPTS/FPTE
                ad += 4;
                x_put_long (ad, 0); // FPTM
                ad += 4;
                x_put_long (ad, regs.exp_pack[0]); // FPTM
                ad += 4;
                x_put_long (ad, 0); // ETS/ETE
                ad += 4;
                x_put_long (ad, regs.exp_pack[1]); // ETM
                ad += 4;
                x_put_long (ad, regs.exp_pack[2]); // ETM
                ad += 4;
            } else {
                x_put_long (ad, src2[0]); // FPTS/FPTE
                ad += 4;
                x_put_long (ad, src2[1]); // FPTM
                ad += 4;
                x_put_long (ad, src2[2]); // FPTM
                ad += 4;
                x_put_long (ad, src1[0]); // ETS/ETE
                ad += 4;
                x_put_long (ad, src1[1]); // ETM
                ad += 4;
                x_put_long (ad, src1[2]); // ETM
                ad += 4;
            }
            if (incr < 0)
                ad -= frame_size;
        }
    } else { /* 68881/68882 */
        int frame_size_real = currprefs.fpu_model == 68882 ? 0x3c : 0x1c;;
        int frame_size = regs.fpu_state == 0 ? 0 : frame_size_real;
        uae_u32 frame_id = regs.fpu_state == 0 ? ((frame_size_real - 4) << 16) : (fpu_version << 24) | ((frame_size_real - 4) << 16);
        
        if (currprefs.mmu_model) {
            if (incr < 0) {
                for (i = 0; i < (frame_size / 4) - 1; i++) {
                    ad -= 4;
                    if (mmu030_state[0] == i) {
                        x_put_long (ad, i == 0 ? 0x70000000 : 0x00000000);
                        mmu030_state[0]++;
                    }
                }
                ad -= 4;
                if (mmu030_state[0] == (frame_size / 4) - 1 || (mmu030_state[0] == 0 && frame_size == 0)) {
                    x_put_long (ad, frame_id);
                    mmu030_state[0]++;
                }
            } else {
                if (mmu030_state[0] == 0) {
                    x_put_long (ad, frame_id);
                    mmu030_state[0]++;
                }
                ad += 4;
                for (i = 0; i < (frame_size / 4) - 1; i++) {
                    if (mmu030_state[0] == i + 1) {
                        x_put_long (ad, i == (frame_size / 4) - 2 ? 0x70000000 : 0x00000000);
                        mmu030_state[0]++;
                    }
                    ad += 4;
                }
            }
        } else {
            if (incr < 0) {
                for (i = 0; i < (frame_size / 4) - 1; i++) {
                    ad -= 4;
                    x_put_long (ad, i == 0 ? 0x70000000 : 0x00000000);
                }
                ad -= 4;
                x_put_long (ad, frame_id);
            } else {
                x_put_long (ad, frame_id);
                ad += 4;
                for (i = 0; i < (frame_size / 4) - 1; i++) {
                    x_put_long (ad, i == (frame_size / 4) - 2 ? 0x70000000 : 0x00000000);
                    ad += 4;
                }
            }
        }
    }
    
    if ((opcode & 0x38) == 0x18)
        m68k_areg (regs, opcode & 7) = ad;
    if ((opcode & 0x38) == 0x20)
        m68k_areg (regs, opcode & 7) = ad;
    regs.fpu_exp_state = 0;
}

void fpuop_restore (uae_u32 opcode)
{
    uaecptr pc = m68k_getpc () - 2;
    uae_u32 ad;
    uae_u32 d;
    int incr = (opcode & 0x38) == 0x20 ? -1 : 1;
    
    regs.fp_exception = false;
    
    if (fault_if_no_6888x (opcode, 0, pc))
        return;
    
    if (get_fp_ad (opcode, &ad) == 0) {
        fpu_op_illg (opcode, 0, pc);
        return;
    }
    
    if (fault_if_no_fpu (opcode, 0, ad, pc))
        return;
    regs.fpiar = pc;
    
    if (incr < 0) {
        ad -= 4;
        d = x_get_long (ad);
    } else {
        d = x_get_long (ad);
        ad += 4;
    }
    
    if (currprefs.fpu_model == 68060) {
        int ff = (d >> 8) & 0xff;
        uae_u32 v1, v2;
        
        if (incr < 0) {
            ad -= 4;
            v1 = x_get_long (ad);
            ad -= 4;
            v2 = x_get_long (ad);
        } else {
            v1 = x_get_long (ad);
            ad += 4;
            v2 = x_get_long (ad);
            ad += 4;
        }
        if (ff == 0x60) {
            regs.fpu_state = 1;
            regs.fpu_exp_state = 0;
        } else if (ff == 0xe0) {
            regs.fpu_exp_state = 1;
            to_exten (&regs.exp_src1, d & 0xffff0000, v1, v2);
        } else if (ff) {
            write_log (_T("FRESTORE invalid frame format %X!\n"), (d >> 8) & 0xff);
        } else {
            fpu_null ();
        }
    } else {
        if ((d & 0xff000000) != 0) {
            regs.fpu_state = 1;
            if (incr < 0)
                ad -= (d >> 16) & 0xff;
            else
                ad += (d >> 16) & 0xff;
        } else {
            fpu_null ();
        }
    }
    
    if ((opcode & 0x38) == 0x18)
        m68k_areg (regs, opcode & 7) = ad;
    if ((opcode & 0x38) == 0x20)
        m68k_areg (regs, opcode & 7) = ad;
}

static uaecptr fmovem2mem (uaecptr ad, uae_u32 list, int incr, int regdir)
{
    int reg;
    
    // 68030 MMU state saving is annoying!
    if (currprefs.mmu_model == 68030) {
        int idx = 0;
        int r;
        int i;
        uae_u32 wrd[3];
        mmu030_state[1] |= MMU030_STATEFLAG1_MOVEM1;
        for (r = 0; r < 8; r++) {
            if (regdir < 0)
                reg = 7 - r;
            else
                reg = r;
            if (list & 0x80) {
                from_exten(&regs.fp[reg], &wrd[0], &wrd[1], &wrd[2]);
                if (incr < 0)
                    ad -= 3 * 4;
                for (i = 0; i < 3; i++) {
                    if (mmu030_state[0] == idx * 3 + i) {
                        if (mmu030_state[1] & MMU030_STATEFLAG1_MOVEM2) {
                            mmu030_state[1] &= ~MMU030_STATEFLAG1_MOVEM2;
                        }
                        else {
                            mmu030_data_buffer = wrd[i];
                            x_put_long(ad + i * 4, wrd[i]);
                        }
                        mmu030_state[0]++;
                    }
                }
                if (incr > 0)
                    ad += 3 * 4;
                idx++;
            }
            list <<= 1;
        }
    } else {
        int r;
        for (r = 0; r < 8; r++) {
            uae_u32 wrd1, wrd2, wrd3;
            if (regdir < 0)
                reg = 7 - r;
            else
                reg = r;
            if (list & 0x80) {
                from_exten(&regs.fp[reg], &wrd1, &wrd2, &wrd3);
                if (incr < 0)
                    ad -= 3 * 4;
                x_put_long(ad + 0, wrd1);
                x_put_long(ad + 4, wrd2);
                x_put_long(ad + 8, wrd3);
                if (incr > 0)
                    ad += 3 * 4;
            }
            list <<= 1;
        }
    }
    return ad;
}

static uaecptr fmovem2fpp (uaecptr ad, uae_u32 list, int incr, int regdir)
{
    int reg;
    
    if (currprefs.mmu_model == 68030) {
        uae_u32 wrd[3];
        int idx = 0;
        int r;
        int i;
        mmu030_state[1] |= MMU030_STATEFLAG1_MOVEM1 | MMU030_STATEFLAG1_FMOVEM;
        if (mmu030_state[1] & MMU030_STATEFLAG1_MOVEM2)
            ad = mmu030_ad[mmu030_idx].val;
        else
            mmu030_ad[mmu030_idx].val = ad;
        for (r = 0; r < 8; r++) {
            if (regdir < 0)
                reg = 7 - r;
            else
                reg = r;
            if (list & 0x80) {
                if (incr < 0)
                    ad -= 3 * 4;
                for (i = 0; i < 3; i++) {
                    if (mmu030_state[0] == idx * 3 + i) {
                        if (mmu030_state[1] & MMU030_STATEFLAG1_MOVEM2) {
                            mmu030_state[1] &= ~MMU030_STATEFLAG1_MOVEM2;
                            wrd[i] = mmu030_data_buffer;
                        } else {
                            wrd[i] = x_get_long (ad + i * 4);
                        }
                        // save first two entries if 2nd or 3rd get_long() faults.
                        if (i == 0 || i == 1)
                            mmu030_fmovem_store[i] = wrd[i];
                        mmu030_state[0]++;
                        if (i == 2)
                            to_exten_fmovem (&regs.fp[reg], mmu030_fmovem_store[0], mmu030_fmovem_store[1], wrd[2]);
                    }
                }
                if (incr > 0)
                    ad += 3 * 4;
                idx++;
            }
            list <<= 1;
        }
    } else {
        int r;
        for (r = 0; r < 8; r++) {
            uae_u32 wrd1, wrd2, wrd3;
            if (regdir < 0)
                reg = 7 - r;
            else
                reg = r;
            if (list & 0x80) {
                if (incr < 0)
                    ad -= 3 * 4;
                wrd1 = x_get_long (ad + 0);
                wrd2 = x_get_long (ad + 4);
                wrd3 = x_get_long (ad + 8);
                if (incr > 0)
                    ad += 3 * 4;
                to_exten_fmovem (&regs.fp[reg], wrd1, wrd2, wrd3);
            }
            list <<= 1;
        }
    }
    return ad;
}

static bool fp_arithmetic(fptype *srcd, int reg, int extra)
{
    fptype fp = *srcd;
    bool sgl = false;
    
    // SNAN -> QNAN if SNAN interrupt is not enabled
    if (fp_is_snan(srcd) && !(regs.fpcr & 0x4000)) {
        fp_unset_snan(srcd);
    }
    
    switch (extra & 0x7f)
    {
        case 0x00: /* FMOVE */
        case 0x40: /* FSMOVE */
        case 0x44: /* FDMOVE */
            regs.fp[reg] = fp;
            break;
        case 0x01: /* FINT */
            regs.fp[reg] = fp_int(fp);
            break;
        case 0x02: /* FSINH */
            regs.fp[reg] = fp_sinh(fp);
            break;
        case 0x03: /* FINTRZ */
            regs.fp[reg] = fp_intrz(fp);
            break;
        case 0x04: /* FSQRT */
        case 0x41: /* FSSQRT */
        case 0x45: /* FDSQRT */
            regs.fp[reg] = fp_sqrt(fp);
            break;
        case 0x06: /* FLOGNP1 */
            regs.fp[reg] = fp_lognp1(fp);
            break;
        case 0x08: /* FETOXM1 */
            regs.fp[reg] = fp_etoxm1(fp);
            break;
        case 0x09: /* FTANH */
            regs.fp[reg] = fp_tanh(fp);
            break;
        case 0x0a: /* FATAN */
            regs.fp[reg] = fp_atan(fp);
            break;
        case 0x0c: /* FASIN */
            regs.fp[reg] = fp_asin(fp);
            break;
        case 0x0d: /* FATANH */
            regs.fp[reg] = fp_atanh(fp);
            break;
        case 0x0e: /* FSIN */
            regs.fp[reg] = fp_sin(fp);
            break;
        case 0x0f: /* FTAN */
            regs.fp[reg] = fp_tan(fp);
            break;
        case 0x10: /* FETOX */
            regs.fp[reg] = fp_etox(fp);
            break;
        case 0x11: /* FTWOTOX */
            regs.fp[reg] = fp_twotox(fp);
            break;
        case 0x12: /* FTENTOX */
            regs.fp[reg] = fp_tentox(fp);
            break;
        case 0x14: /* FLOGN */
            regs.fp[reg] = fp_logn(fp);
            break;
        case 0x15: /* FLOG10 */
            regs.fp[reg] = fp_log10(fp);
            break;
        case 0x16: /* FLOG2 */
            regs.fp[reg] = fp_log2(fp);
            break;
        case 0x18: /* FABS */
        case 0x58: /* FSABS */
        case 0x5c: /* FDABS */
            regs.fp[reg] = fp_abs(fp);
            break;
        case 0x19: /* FCOSH */
            regs.fp[reg] = fp_cosh(fp);
            break;
        case 0x1a: /* FNEG */
        case 0x5a: /* FSNEG */
        case 0x5e: /* FDNEG */
            regs.fp[reg] = fp_neg(fp);
            break;
        case 0x1c: /* FACOS */
            regs.fp[reg] = fp_acos(fp);
            break;
        case 0x1d: /* FCOS */
            regs.fp[reg] = fp_cos(fp);
            break;
        case 0x1e: /* FGETEXP */
            regs.fp[reg] = fp_getexp(fp);
            break;
        case 0x1f: /* FGETMAN */
            regs.fp[reg] = fp_getman(fp);
            break;
        case 0x20: /* FDIV */
        case 0x60: /* FSDIV */
        case 0x64: /* FDDIV */
            regs.fp[reg] = fp_div(regs.fp[reg], fp);
            break;
        case 0x21: /* FMOD */
            regs.fp[reg] = fp_mod(regs.fp[reg], fp);
            break;
        case 0x22: /* FADD */
        case 0x62: /* FSADD */
        case 0x66: /* FDADD */
            regs.fp[reg] = fp_add(regs.fp[reg], fp);
            break;
        case 0x23: /* FMUL */
        case 0x63: /* FSMUL */
        case 0x67: /* FDMUL */
            regs.fp[reg] = fp_mul(regs.fp[reg], fp);
            break;
        case 0x24: /* FSGLDIV */
            regs.fp[reg] = fp_div(regs.fp[reg], fp);
            sgl = true;
            break;
        case 0x25: /* FREM */
            regs.fp[reg] = fp_rem(regs.fp[reg], fp);
            break;
        case 0x26: /* FSCALE */
            regs.fp[reg] = fp_scale(regs.fp[reg], fp);
            break;
        case 0x27: /* FSGLMUL */
            regs.fp[reg] = fp_mul(regs.fp[reg], fp);
            sgl = true;
            break;
        case 0x28: /* FSUB */
        case 0x68: /* FSSUB */
        case 0x6c: /* FDSUB */
            regs.fp[reg] = fp_sub(regs.fp[reg], fp);
            break;
        case 0x30: /* FSINCOS */
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            regs.fp[extra & 7] = fp_cos(fp);
            regs.fp[reg]       = fp_sin(fp);
            if (((regs.fpcr >> 6) & 3) == 1) fp_round32(&regs.fp[extra & 7]);
            if (((regs.fpcr >> 6) & 3) == 2) fp_round64(&regs.fp[extra & 7]);
            break;
        case 0x38: /* FCMP */
            fpsr_set_result(fp_sub(regs.fp[reg], fp));
            return true;
        case 0x3a: /* FTST */
            fpsr_set_result(fp);
            return true;
            
        default:
            return false;
    }
    
    if (sgl)
        fp_roundsgl(&regs.fp[reg]);
    else if ((extra & 0x44) == 0x40)
        fp_round32(&regs.fp[reg]);
    else if ((extra & 0x44) == 0x44)
        fp_round64(&regs.fp[reg]);
    else if (((regs.fpcr >> 6) & 3) == 1)
        fp_round32(&regs.fp[reg]);
    else if (((regs.fpcr >> 6) & 3) == 2)
        fp_round64(&regs.fp[reg]);
    
    fpsr_set_result(regs.fp[reg]);
    
    return true;
}

static void fpuop_arithmetic2 (uae_u32 opcode, uae_u16 extra)
{
    int reg = -1;
    int v;
    fptype srcd;
    uaecptr pc = m68k_getpc () - 4;
    uaecptr ad = 0;
    
#if DEBUG_FPP
    write_log (_T("FPP %04x %04x at %08x\n"), opcode & 0xffff, extra, pc);
#endif
    if (fault_if_no_6888x (opcode, extra, pc))
        return;
    
    switch ((extra >> 13) & 0x7)
    {
        case 3:
            fpsr_clear_status();
            if (put_fp_value (&regs.fp[(extra >> 7) & 7], opcode, extra, pc) == 0)
                fpu_noinst (opcode, pc);
            fpsr_make_status();
            return;
            
        case 4:
        case 5:
            if ((opcode & 0x38) == 0) {
                if (fault_if_no_fpu (opcode, extra, 0, pc))
                    return;
                if (extra & 0x2000) {
                    if (extra & 0x1000)
                        m68k_dreg (regs, opcode & 7) = regs.fpcr & 0xffff;
                    if (extra & 0x0800)
                        m68k_dreg (regs, opcode & 7) = fpp_get_fpsr ();
                    if (extra & 0x0400)
                        m68k_dreg (regs, opcode & 7) = regs.fpiar;
                } else {
                    if (extra & 0x1000)
                        fpp_set_fpcr (m68k_dreg (regs, opcode & 7));
                    if (extra & 0x0800)
                        fpp_set_fpsr (m68k_dreg (regs, opcode & 7));
                    if (extra & 0x0400)
                        regs.fpiar = m68k_dreg (regs, opcode & 7);
                }
            } else if ((opcode & 0x38) == 0x08) {
                if (fault_if_no_fpu (opcode, extra, 0, pc))
                    return;
                if (extra & 0x2000) {
                    if (extra & 0x1000)
                        m68k_areg (regs, opcode & 7) = regs.fpcr & 0xffff;
                    if (extra & 0x0800)
                        m68k_areg (regs, opcode & 7) = fpp_get_fpsr ();
                    if (extra & 0x0400)
                        m68k_areg (regs, opcode & 7) = regs.fpiar;
                } else {
                    if (extra & 0x1000)
                        fpp_set_fpcr (m68k_areg (regs, opcode & 7));
                    if (extra & 0x0800)
                        fpp_set_fpsr (m68k_areg (regs, opcode & 7));
                    if (extra & 0x0400)
                        regs.fpiar = m68k_areg (regs, opcode & 7);
                }
            } else if ((opcode & 0x3f) == 0x3c) {
                if (fault_if_no_fpu (opcode, extra, 0, pc))
                    return;
                if ((extra & 0x2000) == 0) {
                    uae_u32 ext[3];
                    // 68060 FMOVEM.L #imm,more than 1 control register: unimplemented EA
                    uae_u16 bits = extra & (0x1000 | 0x0800 | 0x0400);
                    if (bits && bits != 0x1000 && bits != 0x0800 && bits != 0x400) {
                        if (fault_if_60 (opcode, extra, ad, pc, FPU_EXP_UNIMP_EA))
                            return;
                    }
                    // fetch first, use only after all data has been fetched
                    ext[0] = ext[1] = ext[2] = 0;
                    if (extra & 0x1000)
                        ext[0] = x_cp_next_ilong ();
                    if (extra & 0x0800)
                        ext[1] = x_cp_next_ilong ();
                    if (extra & 0x0400)
                        ext[2] = x_cp_next_ilong ();
                    if (extra & 0x1000)
                        fpp_set_fpcr (ext[0]);
                    if (extra & 0x0800)
                        fpp_set_fpsr (ext[1]);
                    if (extra & 0x0400)
                        regs.fpiar = ext[2];
                }
            } else if (extra & 0x2000) {
                /* FMOVEM FPP->memory */
                uae_u32 ad;
                int incr = 0;
                
                if (get_fp_ad (opcode, &ad) == 0) {
                    fpu_noinst (opcode, pc);
                    return;
                }
                if (fault_if_no_fpu (opcode, extra, ad, pc))
                    return;
                
                if ((opcode & 0x38) == 0x20) {
                    if (extra & 0x1000)
                        incr += 4;
                    if (extra & 0x0800)
                        incr += 4;
                    if (extra & 0x0400)
                        incr += 4;
                }
                ad -= incr;
                if (extra & 0x1000) {
                    x_cp_put_long (ad, regs.fpcr & 0xffff);
                    ad += 4;
                }
                if (extra & 0x0800) {
                    x_cp_put_long (ad, fpp_get_fpsr ());
                    ad += 4;
                }
                if (extra & 0x0400) {
                    x_cp_put_long (ad, regs.fpiar);
                    ad += 4;
                }
                ad -= incr;
                if ((opcode & 0x38) == 0x18)
                    m68k_areg (regs, opcode & 7) = ad;
                if ((opcode & 0x38) == 0x20)
                    m68k_areg (regs, opcode & 7) = ad;
            } else {
                /* FMOVEM memory->FPP */
                uae_u32 ad;
                int incr = 0;
                
                if (get_fp_ad (opcode, &ad) == 0) {
                    fpu_noinst (opcode, pc);
                    return;
                }
                if (fault_if_no_fpu (opcode, extra, ad, pc))
                    return;
                
                if((opcode & 0x38) == 0x20) {
                    if (extra & 0x1000)
                        incr += 4;
                    if (extra & 0x0800)
                        incr += 4;
                    if (extra & 0x0400)
                        incr += 4;
                    ad = ad - incr;
                }
                if (extra & 0x1000) {
                    fpp_set_fpcr (x_cp_get_long (ad));
                    ad += 4;
                }
                if (extra & 0x0800) {
                    fpp_set_fpsr (x_cp_get_long (ad));
                    ad += 4;
                }
                if (extra & 0x0400) {
                    regs.fpiar = x_cp_get_long (ad);
                    ad += 4;
                }
                if ((opcode & 0x38) == 0x18)
                    m68k_areg (regs, opcode & 7) = ad;
                if ((opcode & 0x38) == 0x20)
                    m68k_areg (regs, opcode & 7) = ad - incr;
            }
            return;
            
        case 6:
        case 7:
        {
            uae_u32 ad, list = 0;
            int incr = 1;
            int regdir = 1;
            if (get_fp_ad (opcode, &ad) == 0) {
                fpu_noinst (opcode, pc);
                return;
            }
            if (fault_if_no_fpu (opcode, extra, ad, pc))
                return;
            switch ((extra >> 11) & 3)
            {
                case 0:	/* static pred */
                    list = extra & 0xff;
                    regdir = -1;
                    break;
                case 1:	/* dynamic pred */
                    if (fault_if_60 (opcode, extra, ad, pc, FPU_EXP_UNIMP_EA))
                        return;
                    list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
                    regdir = -1;
                    break;
                case 2:	/* static postinc */
                    list = extra & 0xff;
                    break;
                case 3:	/* dynamic postinc */
                    if (fault_if_60 (opcode, extra, ad, pc, FPU_EXP_UNIMP_EA))
                        return;
                    list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
                    break;
            }
            if ((opcode & 0x38) == 0x20) // -(an)
                incr = -1;
            if (extra & 0x2000) {
                /* FMOVEM FPP->memory */
                ad = fmovem2mem (ad, list, incr, regdir);
            } else {
                /* FMOVEM memory->FPP */
                ad = fmovem2fpp (ad, list, incr, regdir);
            }
            if ((opcode & 0x38) == 0x18 || (opcode & 0x38) == 0x20)
                m68k_areg (regs, opcode & 7) = ad;
        }
            return;
            
        case 0:
        case 2: /* Extremely common */
            regs.fpiar = pc;
            reg = (extra >> 7) & 7;
            if ((extra & 0xfc00) == 0x5c00) {
                if (fault_if_no_fpu (opcode, extra, 0, pc))
                    return;
                if (fault_if_unimplemented_680x0 (opcode, extra, ad, pc, &srcd, reg))
                    return;
                fpsr_clear_status();
                if (!fpu_get_constant(&regs.fp[reg], extra)) {
                    fpu_noinst(opcode, pc);
                    return;
                }
                fpsr_set_result(regs.fp[reg]);
                fpsr_make_status();
                return;
            }
            
            // 6888x does not have special exceptions, check immediately
            if (fault_if_unimplemented_6888x (opcode, extra, pc))
                return;
            
            fpsr_clear_status();

            v = get_fp_value (opcode, extra, &srcd, pc, &ad);
            if (v <= 0) {
                if (v == 0)
                    fpu_noinst (opcode, pc);
                return;
            }
            
            // get_fp_value() checked this, but only if EA was nonzero (non-register)
            if (fault_if_unimplemented_680x0 (opcode, extra, ad, pc, &srcd, reg))
                return;
            
            regs.fpiar =  pc;
            
            v = fp_arithmetic(&srcd, reg, extra);
            if (!v)
                fpu_noinst (opcode, pc);
            fpsr_make_status();
            return;
        default:
            break;
    }
    fpu_noinst (opcode, pc);
}

void fpuop_arithmetic (uae_u32 opcode, uae_u16 extra)
{
    regs.fpu_state = 1;
    regs.fp_exception = false;
    fpu_mmu_fixup = false;
    fpuop_arithmetic2 (opcode, extra);
    if (fpu_mmu_fixup) {
        mmufixup[0].reg = -1;
    }
}

void fpu_reset (void)
{
    regs.fpiar = 0;
    regs.fpu_exp_state = 0;
    fpp_set_fpcr (0);
    fpp_set_fpsr (0);
    fpset (&regs.fp_result, 1);
}

#if 0
uae_u8 *restore_fpu (uae_u8 *src)
{
    uae_u32 w1, w2, w3;
    int i;
    uae_u32 flags;
    
    fpu_reset();
    changed_prefs.fpu_model = currprefs.fpu_model = restore_u32 ();
    flags = restore_u32 ();
    for (i = 0; i < 8; i++) {
        w1 = restore_u16 () << 16;
        w2 = restore_u32 ();
        w3 = restore_u32 ();
        to_exten (&regs.fp[i], w1, w2, w3);
    }
    regs.fpcr = restore_u32 ();
    native_set_fpucw (regs.fpcr);
    regs.fpsr = restore_u32 ();
    regs.fpiar = restore_u32 ();
    if (flags & 0x80000000) {
        restore_u32 ();
        restore_u32 ();
    }
    if (flags & 0x40000000) {
        w1 = restore_u16() << 16;
        w2 = restore_u32();
        w3 = restore_u32();
        to_exten(&regs.exp_src1, w1, w2, w3);
        w1 = restore_u16() << 16;
        w2 = restore_u32();
        w3 = restore_u32();
        to_exten(&regs.exp_src2, w1, w2, w3);
        regs.exp_pack[0] = restore_u32();
        regs.exp_pack[1] = restore_u32();
        regs.exp_pack[2] = restore_u32();
        regs.exp_opcode = restore_u16();
        regs.exp_extra = restore_u16();
        regs.exp_type = restore_u16();
    }
    regs.fpu_state = (flags & 1) ? 0 : 1;
    regs.fpu_exp_state = (flags & 2) ? 1 : 0;
    if (flags & 4)
        regs.fpu_exp_state = 2;
    write_log(_T("FPU: %d\n"), currprefs.fpu_model);
    return src;
}

uae_u8 *save_fpu (int *len, uae_u8 *dstptr)
{
    uae_u32 w1, w2, w3;
    uae_u8 *dstbak, *dst;
    int i;
    
    *len = 0;
#ifndef WINUAE_FOR_HATARI
    /* Under Hatari, we save all FPU variables, even if fpu_model==0 */
    if (currprefs.fpu_model == 0)
        return 0;
#endif
    if (dstptr)
        dstbak = dst = dstptr;
    else
        dstbak = dst = xmalloc (uae_u8, 4+4+8*10+4+4+4+4+4+2*10+3*(4+2));
    save_u32 (currprefs.fpu_model);
    save_u32 (0x80000000 | 0x40000000 | (regs.fpu_state == 0 ? 1 : 0) | (regs.fpu_exp_state ? 2 : 0) | (regs.fpu_exp_state > 1 ? 4 : 0));
    for (i = 0; i < 8; i++) {
        from_exten (&regs.fp[i], &w1, &w2, &w3);
        save_u16 (w1 >> 16);
        save_u32 (w2);
        save_u32 (w3);
    }
    save_u32 (regs.fpcr);
    save_u32 (regs.fpsr);
    save_u32 (regs.fpiar);
    
    save_u32 (-1);
    save_u32 (0);
    
    from_exten(&regs.exp_src1, &w1, &w2, &w3);
    save_u16(w1 >> 16);
    save_u32(w2);
    save_u32(w3);
    from_exten(&regs.exp_src2, &w1, &w2, &w3);
    save_u16(w1 >> 16);
    save_u32(w2);
    save_u32(w3);
    save_u32(regs.exp_pack[0]);
    save_u32(regs.exp_pack[1]);
    save_u32(regs.exp_pack[2]);
    save_u16(regs.exp_opcode);
    save_u16(regs.exp_extra);
    save_u16(regs.exp_type);
    
    *len = dst - dstbak;
    return dstbak;
}
#endif