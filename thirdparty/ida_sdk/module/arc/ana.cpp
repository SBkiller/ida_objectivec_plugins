/*
 *      Interactive disassembler (IDA).
 *      Copyright (c) 2012 Hex-Rays
 *      ALL RIGHTS RESERVED.
 *
 *      ARC (Argonaut RISC Core) processor module
 *
 *      Based on code contributed by by Felix Domke <tmbinc@gmx.net>
 */

#include "arc.hpp"
#include <frame.hpp>

/*
doRegisterOperand converts the 6 bit field 'code' to an IDA-"op_t"-operand.

'd' is the maybe-used (signed) immediate in the lowest 9 bits, li is the
long-immediate which is loaded in the instruction decoding, since it's
loaded only once, even if an instructions uses multiple times a long immediate

when it's all about a branch (isbranch is true), we have to multiply the absolute
address by 4, since it's granularity are words then (and not bytes)

FYI:
register code 61 means "short immediate with .f-flag set", 63 "short immediate
without .f-flag" and 62 means "long immediate (4 bytes following the instruction,
making the instruction 8 bytes long (cmd.size)).
*/

//----------------------------------------------------------------------
void doRegisterOperand(int code, op_t &op, int d, int li, int isbranch)
{
  /* we always deal with double words, exceptions are load/stores
     with 8 or 16 bits. these are covered by the instruction decoding */

  op.dtyp = dt_dword;
  if ( code == SHIMM_F || code == SHIMM )     // short immediate with/wo flags
  {
    if ( isbranch )
    {
      op.type = o_near;
      op.addr = d * 4;
    }
    else
    {
      op.type = o_imm;
      op.value = d;
    }
  }
  else if ( code == LIMM )          // long immediate
  {
    if ( isbranch )
    {
      op.type = o_near;
      /* the upper 7 bits containing processor flags to set  */
      /* they are handled in the instruction decoding, since */
      /* they produce a second (ida-)operand */
      op.addr = (li & 0x1FFFFFF) * 4;
    }
    else
    {
      op.type = o_imm;
      op.value = li;
    }
    op.offb = 4;
  }
  else                          /* just a register */
  {
    op.type = o_reg;
    op.reg = uint16(code);
  }
}

//----------------------------------------------------------------------
// make indirect [b,c] operand
//  b   c
// imm imm  mem:   [imm1+imm2]
// reg imm  displ: [reg, imm]
// imm reg  displ: [imm, reg] (membase=1)
// reg reg  phrase: [reg, reg]
void doIndirectOperand(int b, int c, op_t &op, int d, int li, bool special)
{
   if ( is_imm(b) && is_imm(c) )
   {
     // [#imm, #imm]
     int imm1 = b == LIMM ? li : d;
     int imm2 = c == LIMM ? li : d;
     if ( special )
       imm2 = 0;
     op.type = o_mem;
     op.addr = imm1 + imm2;
   }
   else if ( !is_imm(b) && !is_imm(c) )
   {
     // [reg, reg]
     op.type = o_phrase;
     op.reg = b;
     op.secreg = c;
   }
   else
   {
     op.type = o_displ;
     if ( is_imm(c) )
     {
       // [reg, #imm]
       op.reg = b;
       op.addr = c == LIMM ? li : d;
       if ( special )
         op.addr = 0;
       op.membase = 0;
     }
     else
     {
       // [#imm, reg]
       op.reg = c;
       op.addr = b == LIMM ? li : d;
       op.membase = 1;
     }
   }
   switch ( cmd.auxpref & aux_zmask )
   {
     default:
       op.dtyp = dt_dword;
       break;
     case aux_b:
       op.dtyp = dt_byte;
       break;
     case aux_w:
       op.dtyp = dt_word;
       break;
   }
}

//----------------------------------------------------------------------
// doBranchOperand handles pc-relative word offsets.
// nothing special here.
void doBranchOperand(op_t &op, int l)
{
  op.dtyp = dt_dword;
  op.type = o_near;
  op.addr = cmd.ip + l * 4 + 4;
  op.offb = 0;
}

void doRegisterInstruction(uint32 code)
{

  int i = (code >> 27) & 31;
  int a = (code >> 21) & 63;
  int b = (code >> 15) & 63;
  int c = (code >> 9)  & 63;

  /* the (maybe used?) short immediate value */
  int d = code & 0x1FF;

  // sign-extend
  if ( d >= 0x100 )
    d -= 0x200;

  /* store the flags. if there are actually no flags at that place, they */
  /* will be reconstructed later */
  cmd.auxpref = code & 0x1FF;

  switch ( i )
  {
    case 0:                    // LD register+register
      cmd.itype = ARC_ld;
      break;
    case 1:                    // LD register+offset, LR
      if ( code & (1 << 13) )
        cmd.itype = ARC_lr;
      else
        cmd.itype = ARC_ld;
      break;
    case 2:                    // ST, SR
      if ( code & (1 << 25) )
        cmd.itype = ARC_sr;
      else
        cmd.itype = ARC_st;
      break;
    case 3:                    // single operand instructions
      switch ( c )
      {
        case 0:
          cmd.itype = ARC_flag;
          a = b;                // flag has no 'a' operand, so we're moving the b-operand to a.
          break;
        case 1:
          cmd.itype = ARC_asr;
          break;
        case 2:
          cmd.itype = ARC_lsr;
          break;
        case 3:
          cmd.itype = ARC_ror;
          break;
        case 4:
          cmd.itype = ARC_rrc;
          break;
        case 5:
          cmd.itype = ARC_sexb;
          break;
        case 6:
          cmd.itype = ARC_sexw;
          break;
        case 7:
          cmd.itype = ARC_extb;
          break;
        case 8:
          cmd.itype = ARC_extw;
          break;
        case 9:
          cmd.itype = ARC_swap;
          break;
        case 10:
          cmd.itype = ARC_norm;
          break;
        case 0x3F:
          switch ( d )
          {
            case 0:
              cmd.itype = ARC_brk;
              break;
            case 1:
              cmd.itype = ARC_sleep;
              break;
            case 2:
              cmd.itype = ARC_swi;
              break;
            default:
              return;
          }
          a = b = -1;
          cmd.auxpref = 0;
          break;
      }
      c = -1;                   // c operand is no real operand, so don't try to convert it.
      break;
    case 7:                    // Jcc, JLcc
      cmd.itype = ARC_j;
      if ( code & (1<<9) )
        cmd.itype = ARC_jl;
      else
        cmd.itype = ARC_j;
      break;
    case 8:                    // ADD
      cmd.itype = ARC_add;
      break;
    case 9:                    // ADC
      cmd.itype = ARC_adc;
      break;
    case 10:                   // SUB
      cmd.itype = ARC_sub;
      break;
    case 11:                   // SBC
      cmd.itype = ARC_sbc;
      break;
    case 12:                   // AND
      cmd.itype = ARC_and;
      break;
    case 13:                   // OR
      cmd.itype = ARC_or;
      break;
    case 14:                   // BIC
      cmd.itype = ARC_bic;
      break;
    case 15:                   // XOR
      cmd.itype = ARC_xor;
      break;
    case 0x10:
      cmd.itype = ARC_asl;
      break;
    case 0x11:
      cmd.itype = ARC_lsr;
      break;
    case 0x12:
      cmd.itype = ARC_asr;
      break;
    case 0x13:
      cmd.itype = ARC_ror;
      break;
    case 0x14:
      cmd.itype = ARC_mul64;
      break;
    case 0x15:
      cmd.itype = ARC_mulu64;
      break;
    case 0x1E:
      cmd.itype = ARC_max;
      break;
    case 0x1F:
      cmd.itype = ARC_min;
      break;
  }

  uint32 immediate = 0;
  int noop3 = 0, isnop = 0;

  if ( a == SHIMM_F || b == SHIMM_F || c == SHIMM_F )
    cmd.auxpref = aux_f;       // .f

  if ( b == SHIMM || c == SHIMM )
    cmd.auxpref = 0;

  if ( b == LIMM || c == LIMM )
    immediate = ua_next_long();

  /*
  pseudo instruction heuristic:

  we have some types of pseudo-instructions:

  (rS might be an immediate)
  insn                    will be coded as
  move rD, rS             and rD, rS, rS
  asl rD, rS              add rD, rS, rS
  lsl rD, rS              add rD, rS, rS (the same as asl, of course...)
  rlc rD, rS              adc.f rD, rS, rS
  rol rD, rS              add.f rD, rS, rS; adc rD, rD, 0
  nop                     xxx 0, 0, 0
  */

  switch ( cmd.itype )
  {
    case ARC_flag:
      // special handling for flag, since its a-operand is a source here
      b = -1;
      break;

    case ARC_and:
    case ARC_or:
      if ( b == c )
      {
        noop3 = 1;
        cmd.itype = ARC_mov;
      }
      break;

    case ARC_add:
      if ( b == c )
      {
        noop3 = 1;
        if ( b >= SHIMM_F )
        {
          // add rD, imm, imm -> move rD, imm*2
          cmd.itype = ARC_mov;
          d <<= 1;
          immediate <<= 1;
        }
        else
        {
          cmd.itype = ARC_lsl;
        }
      }
      break;

    case ARC_adc:
      if ( b == c )
      {
        noop3 = 1;
        cmd.itype = ARC_rlc;
      }
      break;

    case ARC_xor:
      if ( code == 0x7FFFFFFF) // XOR 0x1FF, 0x1FF, 0x1FF
        isnop = 1;
      break;
  }

  //if ( (i>=8) && (i != 0x14) && (i!=0x15) && (a>62) && !(cmd.auxpref&(1<<8)) )        // 3 operands, but target is immediate and no flags to set
  //  isnop=1;

  if ( !isnop )
  {
    if ( i == 0 )
    {
      // ld a, [b,c]
      doRegisterOperand(a, cmd.Op1, d, immediate, 0);
      doIndirectOperand(b, c, cmd.Op2, d, immediate, false);
    }
    else if ( i == 1 || i == 2 )
    {
      /* fetch the flag-bits from the right location */
      if ( cmd.itype == ARC_ld )
        cmd.auxpref = (code >> 9) & 0x3F;
      else if ( cmd.itype == ARC_st )
        cmd.auxpref = (code >> 21) & 0x3F;
      else
        cmd.auxpref = 0;
      if ( cmd.itype == ARC_st || cmd.itype == ARC_sr )
      {
        /* in a move to special register or load from special register,
           we have the target operand somewhere else */
        a = c;
        /* c=-1; not used anyway */
      }
      doRegisterOperand(a, cmd.Op1, d, immediate, 0);
      doIndirectOperand(b, SHIMM, cmd.Op2, d, immediate, cmd.itype == ARC_lr || cmd.itype == ARC_sr);
    }
    else if ( i == 7 )
    {
      /* the jump (absolute) instruction, with a special imm-encoding */
      doRegisterOperand(b, cmd.Op1, d, immediate, 1);
    }
    else
    {
      if ( a != -1 )
        doRegisterOperand(a, cmd.Op1, 0, immediate, 0);
      /* this is a bugfix for the gnu-as: long immediate must be equal, while short */
      /* immediates don't have to. */
      if ( b != -1 )
        doRegisterOperand(b, cmd.Op2, d, immediate, 0);
      if ( c != -1  && !noop3 )
        doRegisterOperand(c, cmd.Op3, d, immediate, 0);
    }
  }
  else
  {
    cmd.itype = ARC_nop;
    cmd.auxpref = 0;
  }
}

void doBranchInstruction(uint32 code)
{
  int i = (code >> 27) & 31;

  int l = (code >> 7) & 0xFFFFF;  // note: bits 21..2, so it's in WORDS

  if ( l >= 0x80000 )             // sign-extend
    l = l - 0x100000;

  doBranchOperand(cmd.Op1, l);

  switch ( i )
  {
    case 4:                    // Bcc
      cmd.itype = ARC_b;
      break;
    case 5:                    // BLcc
      cmd.itype = ARC_bl;
      break;
    case 6:                    // LPcc
      cmd.itype = ARC_lp;
      break;
  }
  cmd.auxpref = code & 0x1FF;
}

//----------------------------------------------------------------------
// analyze ARCTangent-A4 (32-bit) instruction
static int ana_old(void)
{
  if ( cmd.ea & 3 )
    return 0;

  cmd.Op1.dtyp = dt_dword;
  cmd.Op2.dtyp = dt_dword;
  cmd.Op3.dtyp = dt_dword;

  uint32 code = ua_next_long();

  int i = (code >> 27) & 31;

  cmd.itype = 0;

  switch (i)
  {
    case 0:                    // LD register+register
    case 1:                    // LD register+offset, LR
    case 2:                    // ST, SR
    case 3:                    // single operand instructions
      doRegisterInstruction(code);
      break;
    case 4:                    // Bcc
    case 5:                    // BLcc
    case 6:                    // LPcc
      doBranchInstruction(code);
      break;
    case 7:                    // Jcc, JLcc
    case 8:                    // ADD
    case 9:                    // ADC
    case 10:                   // SUB
    case 11:                   // ADC
    case 12:                   // AND
    case 13:                   // OR
    case 14:                   // BIC
    case 15:                   // XOR
      default:
      doRegisterInstruction(code);
      break;
  }

  return cmd.size;
}

#define SUBTABLE(high, low, name) (0x80000000 | (high << 8) | low), 0, {0,0,0}, name
#define SUBTABLE2(high1, low1, high2, low2, name) (0x80000000 | (high1 << 24) | (low1 << 16) | (high2 << 8) | low2), 0, {0,0,0}, name

//----------------------------------------------------------------------
struct arcompact_opcode_t
{
  uint32 mnem;   // instruction itype, or indicator of sub-field decoding
  uint32 aux;    // auxpref and other flags
  uint32 ops[3]; // description of operands
  const struct arcompact_opcode_t *subtable; //lint !e958 padding is required to align members
};

enum aux_flags_t
{
  AUX_B = 1,          // implicit byte size access
  AUX_W = 2,          // implicit word size access
  Q_4_0 = 4,          //  4..0 QQQQQ condition code
  AAZZXD_23_15 = 8,   //  23..22,18..15  aa, ZZ, X, D flags (load reg+reg)
  DAAZZX_11_6 = 0x10, // 11..6   Di, aa, ZZ, X flags (load)
  DAAZZR_5_0  = 0x20, //  5..0   Di, aa, ZZ, R flags (store)
  AUX_D  = 0x40,      // implicit delay slot (.d)
  AUX_X  = 0x80,      // implicit sign extend (.x)
  AUX_CND = 0x100,    // implicit condition (in low 5 bits)
  N_5     = 0x200,    //  5..5     N delay slot bit
  AUX_GEN  = 0x400,    // 4..0 = Q if 23..22=0x3, bit 15 = F
  AUX_GEN2 = 0x800,    // 4..0 = Q if 23..22=0x3
};

enum op_fields_t
{
  fA32=1,         //  5..0                   a register operand (6 bits, r0-r63)
  fA16,           //  2..0                   a register operand (3 bits, r0-r3, r12-r15)
  fB32,           // 14..12 & 26..24         b register operand (6 bits)
  fB16,           // 10..8                   b register operand (3 bits)
  fC32,           // 11..6                   c register operand (6 bits)
  fC16,           //  7..5                   c register operand (3 bits)
  fH16,           //  2..0 & 7..5            h register operand (6 bits)
  S25,            // 15..6 & 26..17 & 0..3 s25 signed branch displacement
  S21,            // 15..6 & 26..17        s21 signed branch displacement
  S25L,           // 15..6 & 26..18 & 0..3 s25 signed branch displacement for branch and link
  S21L,           // 15..6 & 26..18        s21 signed branch displacement for branch and link
  S10,            //  8..0                 s10 signed branch displacement
  S9,             // 15..15 & 23..17        s9 signed branch displacement
  S8,             //  6..0                  s8 signed branch displacement
  S7,             //  5..0                  s7 signed branch displacement
  S13,            // 10..0                 s13 signed branch displacement
  U3,             //  2..0                  u2 unsigned immediate
  U5,             //  4..0                  u5 unsigned immediate
  U6,             // 11..6                  u6 unsigned immediate
  U7,             //  6..0                  u7 unsigned immediate
  U7L,            //  4..0                  u7 unsigned immediate (u5*4)
  U8,             //  7..0                  u8 unsigned immediate
  SP_U7,          //  4..0                 [SP, u7]   stack + offset (u7 = u5*4)
  PCL_U10,        //  7..0                 [PCL, u10] PCL + offset (u8*4)
  fB_U5,          //  10..8 & 4..0         [b, u5]
  fB_U6,          //  10..8 & 4..0         [b, u6] (u6=u5*2)
  fB_U7,          //  10..8 & 4..0         [b, u7] (u6=u5*4)
  fB_S9,          //  14..12&26..26, 15&23..16   [b, s9]
  GENA,           //  5..0
  GENB,           //  14..12 & 26..24
  GENC,           // 11..6 or 5..0 & 11..6
  GENC_PCREL,     // 11..6 or 5..0 & 11..6
  fBC_IND,        //  14..12 & 26..24, 11..6  [b, c]
  fBC16_IND,      //  10..8, 7..5  [b, c]
  R_SP,           // implicit SP
  R_BLINK,        // implicit BLINK
  O_ZERO,         // implicit immediate 0
  R_R0,           // implicit R0
  R_GP,           // implicit GP
  GP_S9,          //  8..0                 [GP, s9]   GP + offset
  GP_S10,         //  8..0                 [GP, s10]  GP + offset (s10 = s9*2)
  GP_S11,         //  8..0                 [GP, s11]  GP + offset (s11 = s9*4)
  S11,            //  8..0                  s11 signed immediate (s11 = s9*4)

  O_IND = 0x80000000, // [reg], [imm] (jumps: only [reg])
};

// indexed by bit 16 (maj = 0)
static const struct arcompact_opcode_t arcompact_maj0[2] = {
  { ARC_b, Q_4_0 | N_5, {S21, 0, 0}, NULL }, // 0
  { ARC_b, N_5        , {S25, 0, 0}, NULL }, // 1
};

// indexed by bit 17 (maj = 1, b16 = 0)
static const struct arcompact_opcode_t arcompact_bl[2] = {
  { ARC_bl, Q_4_0 | N_5, {S21L, 0, 0}, NULL }, // 0
  { ARC_bl, N_5        , {S25L, 0, 0}, NULL }, // 1
};

// indexed by bits 3..0 (maj = 1, b16 = 1, b4 = 0)
static const struct arcompact_opcode_t arcompact_br_regreg[0x10] = {
  { ARC_br, AUX_CND|cEQ|N_5, {fB32, fC32, S9}, NULL }, // 0x00
  { ARC_br, AUX_CND|cNE|N_5, {fB32, fC32, S9}, NULL }, // 0x01
  { ARC_br, AUX_CND|cLT|N_5, {fB32, fC32, S9}, NULL }, // 0x02
  { ARC_br, AUX_CND|cGE|N_5, {fB32, fC32, S9}, NULL }, // 0x03
  { ARC_br, AUX_CND|cLO|N_5, {fB32, fC32, S9}, NULL }, // 0x04
  { ARC_br, AUX_CND|cHS|N_5, {fB32, fC32, S9}, NULL }, // 0x05
  { 0 },                                               // 0x06
  { 0 },                                               // 0x07
  { 0 },                                               // 0x08
  { 0 },                                               // 0x09
  { 0 },                                               // 0x0A
  { 0 },                                               // 0x0B
  { 0 },                                               // 0x0C
  { 0 },                                               // 0x0D
  { ARC_bbit0, N_5,          {fB32, fC32, S9}, NULL }, // 0x0E
  { ARC_bbit1, N_5,          {fB32, fC32, S9}, NULL }, // 0x0F
};

// indexed by bits 3..0 (maj = 1, b16 = 1, b4 = 1)
static const struct arcompact_opcode_t arcompact_br_regimm[0x10] = {
  { ARC_br, AUX_CND|cEQ|N_5, {fB32, U6, S9}, NULL }, // 0x00
  { ARC_br, AUX_CND|cNE|N_5, {fB32, U6, S9}, NULL }, // 0x01
  { ARC_br, AUX_CND|cLT|N_5, {fB32, U6, S9}, NULL }, // 0x02
  { ARC_br, AUX_CND|cGE|N_5, {fB32, U6, S9}, NULL }, // 0x03
  { ARC_br, AUX_CND|cLO|N_5, {fB32, U6, S9}, NULL }, // 0x04
  { ARC_br, AUX_CND|cHS|N_5, {fB32, U6, S9}, NULL }, // 0x05
  { 0 },                                             // 0x06
  { 0 },                                             // 0x07
  { 0 },                                             // 0x08
  { 0 },                                             // 0x09
  { 0 },                                             // 0x0A
  { 0 },                                             // 0x0B
  { 0 },                                             // 0x0C
  { 0 },                                             // 0x0D
  { ARC_bbit0, N_5,          {fB32, U6, S9}, NULL }, // 0x0E
  { ARC_bbit1, N_5,          {fB32, U6, S9}, NULL }, // 0x0F
};

// indexed by bit 4 (maj = 1, b16 = 1)
static const struct arcompact_opcode_t arcompact_br[4] = {
  { SUBTABLE( 3,  0, arcompact_br_regreg)       }, // 0
  { SUBTABLE( 3,  0, arcompact_br_regimm)       }, // 1
};

// indexed by bit 16 (maj = 1)
static const struct arcompact_opcode_t arcompact_maj1[0x40] = {
  { SUBTABLE(17, 17, arcompact_bl)              }, // 0
  { SUBTABLE( 4,  4, arcompact_br)              }, // 1
};

// indexed by bits 14..12 & 26..24 (maj = 4, 21..16=0x2F, 5..0=0x3F)
static const struct arcompact_opcode_t arcompact_zop[0x40] = {
  { 0 },                                  // 0x00
  { ARC_sleep, 0,   {GENC, 0, 0}, NULL }, // 0x01
  { ARC_swi,   0,   {   0, 0, 0}, NULL }, // 0x02
  { ARC_sync,  0,   {   0, 0, 0}, NULL }, // 0x03
  { ARC_rtie,  0,   {   0, 0, 0}, NULL }, // 0x04
  { ARC_brk,   0,   {   0, 0, 0}, NULL }, // 0x05
  { 0 },                                  // 0x06
  { 0 },                                  // 0x07
  { 0 },                                  // 0x08
  { 0 },                                  // 0x09
  { 0 },                                  // 0x0A
  { 0 },                                  // 0x0B
  { 0 },                                  // 0x0C
  { 0 },                                  // 0x0D
  { 0 },                                  // 0x0E
  { 0 },                                  // 0x0F
  { 0 },                                  // 0x20
  { 0 },                                  // 0x21
  { 0 },                                  // 0x22
  { 0 },                                  // 0x23
  { 0 },                                  // 0x24
  { 0 },                                  // 0x25
  { 0 },                                  // 0x26
  { 0 },                                  // 0x27
  { 0 },                                  // 0x28
  { 0 },                                  // 0x29
  { 0 },                                  // 0x2A
  { 0 },                                  // 0x2B
  { 0 },                                  // 0x2C
  { 0 },                                  // 0x2D
  { 0 },                                  // 0x2E
  { 0 },                                  // 0x2F
  { 0 },                                  // 0x30
  { 0 },                                  // 0x31
  { 0 },                                  // 0x32
  { 0 },                                  // 0x33
  { 0 },                                  // 0x34
  { 0 },                                  // 0x35
  { 0 },                                  // 0x36
  { 0 },                                  // 0x37
  { 0 },                                  // 0x38
  { 0 },                                  // 0x39
  { 0 },                                  // 0x3A
  { 0 },                                  // 0x3B
  { 0 },                                  // 0x3C
  { 0 },                                  // 0x3D
  { 0 },                                  // 0x3E
  { 0 },                                  // 0x3F
};

// indexed by bits 5..0 (maj = 4, 21..16=0x2F)
static const struct arcompact_opcode_t arcompact_sop[0x40] = {
  { ARC_asl,  0,   {GENB, GENC,    0}, NULL }, // 0x00
  { ARC_asr,  0,   {GENB, GENC,    0}, NULL }, // 0x01
  { ARC_lsr,  0,   {GENB, GENC,    0}, NULL }, // 0x02
  { ARC_ror,  0,   {GENB, GENC,    0}, NULL }, // 0x03
  { ARC_rrc,  0,   {GENB, GENC,    0}, NULL }, // 0x04
  { ARC_sexb, 0,   {GENB, GENC,    0}, NULL }, // 0x05
  { ARC_sexw, 0,   {GENB, GENC,    0}, NULL }, // 0x06
  { ARC_extb, 0,   {GENB, GENC,    0}, NULL }, // 0x07
  { ARC_extw, 0,   {GENB, GENC,    0}, NULL }, // 0x08
  { ARC_abs,  0,   {GENB, GENC,    0}, NULL }, // 0x09
  { ARC_not,  0,   {GENB, GENC,    0}, NULL }, // 0x0A
  { ARC_rlc,  0,   {GENB, GENC,    0}, NULL }, // 0x0B
  { ARC_ex,   0,   {GENB, GENC|O_IND,0},NULL}, // 0x0C
  { 0 },                                       // 0x0D
  { 0 },                                       // 0x0E
  { 0 },                                       // 0x0F
  { 0 },                                       // 0x10
  { 0 },                                       // 0x11
  { 0 },                                       // 0x12
  { 0 },                                       // 0x13
  { 0 },                                       // 0x14
  { 0 },                                       // 0x15
  { 0 },                                       // 0x16
  { 0 },                                       // 0x17
  { 0 },                                       // 0x18
  { 0 },                                       // 0x19
  { 0 },                                       // 0x1A
  { 0 },                                       // 0x1B
  { 0 },                                       // 0x1C
  { 0 },                                       // 0x1D
  { 0 },                                       // 0x1E
  { 0 },                                       // 0x1F
  { 0 },                                       // 0x20
  { 0 },                                       // 0x21
  { 0 },                                       // 0x22
  { 0 },                                       // 0x23
  { 0 },                                       // 0x24
  { 0 },                                       // 0x25
  { 0 },                                       // 0x26
  { 0 },                                       // 0x27
  { 0 },                                       // 0x28
  { 0 },                                       // 0x29
  { 0 },                                       // 0x2A
  { 0 },                                       // 0x2B
  { 0 },                                       // 0x2C
  { 0 },                                       // 0x2D
  { 0 },                                       // 0x2E
  { 0 },                                       // 0x2F
  { 0 },                                       // 0x30
  { 0 },                                       // 0x31
  { 0 },                                       // 0x32
  { 0 },                                       // 0x33
  { 0 },                                       // 0x34
  { 0 },                                       // 0x35
  { 0 },                                       // 0x36
  { 0 },                                       // 0x37
  { 0 },                                       // 0x38
  { 0 },                                       // 0x39
  { 0 },                                       // 0x3A
  { 0 },                                       // 0x3B
  { 0 },                                       // 0x3C
  { 0 },                                       // 0x3D
  { 0 },                                       // 0x3E
  { SUBTABLE2(14, 12, 26, 24, arcompact_zop)}, // 0x3F
};

// indexed by bits 21..16 (maj = 4)
static const struct arcompact_opcode_t arcompact_maj4[0x40] = {
  { ARC_add,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x00
  { ARC_adc,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x01
  { ARC_sub,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x02
  { ARC_sbc,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x03
  { ARC_and,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x04
  { ARC_or,   AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x05
  { ARC_bic,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x06
  { ARC_xor,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x07
  { ARC_max,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x08
  { ARC_min,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x09
  { ARC_mov,  AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x0A
  { ARC_tst,  AUX_GEN2,{GENB, GENC,    0}, NULL }, // 0x0B
  { ARC_cmp,  AUX_GEN2,{GENB, GENC,    0}, NULL }, // 0x0C
  { ARC_rcmp, AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x0D
  { ARC_rsub, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x0E
  { ARC_bset, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x0F
  { ARC_bclr, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x10
  { ARC_btst, AUX_GEN2,{GENB, GENC,    0}, NULL }, // 0x11
  { ARC_bxor, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x12
  { ARC_bmsk, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x13
  { ARC_add1, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x14
  { ARC_add2, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x15
  { ARC_add3, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x16
  { ARC_sub1, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x17
  { ARC_sub2, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x18
  { ARC_sub3, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x19
  { ARC_mpy,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x1A
  { ARC_mpyh, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x1B
  { ARC_mpyhu,AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x1C
  { ARC_mpyu, AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x1D
  { 0 },                                       // 0x1E
  { 0 },                                       // 0x1F
  { ARC_j,  AUX_GEN,       {GENC|O_IND, 0, 0}, NULL }, // 0x20
  { ARC_j,  AUX_GEN|AUX_D, {GENC|O_IND, 0, 0}, NULL }, // 0x21
  { ARC_jl, AUX_GEN,       {GENC|O_IND, 0, 0}, NULL }, // 0x22
  { ARC_jl, AUX_GEN|AUX_D, {GENC|O_IND, 0, 0}, NULL }, // 0x23
  { 0 },                                       // 0x24
  { 0 },                                       // 0x25
  { 0 },                                       // 0x26
  { 0 },                                       // 0x27
  { ARC_lp,   AUX_GEN2,{GENC_PCREL, 0, 0}, NULL }, // 0x28
  { ARC_flag, AUX_GEN2,{GENC,       0, 0}, NULL }, // 0x29
  { ARC_lr,   0,{GENB, GENC|O_IND, 0}, NULL }, // 0x2A
  { ARC_sr,   0,{GENB, GENC|O_IND, 0}, NULL }, // 0x2B
  { 0 },                                       // 0x2C
  { 0 },                                       // 0x2D
  { 0 },                                       // 0x2E
  { SUBTABLE(5, 0, arcompact_sop)           }, // 0x2F
  { ARC_ld, AAZZXD_23_15, {fA32, fBC_IND, 0}, NULL }, // 0x30
  { ARC_ld, AAZZXD_23_15, {fA32, fBC_IND, 0}, NULL }, // 0x31
  { ARC_ld, AAZZXD_23_15, {fA32, fBC_IND, 0}, NULL }, // 0x32
  { ARC_ld, AAZZXD_23_15, {fA32, fBC_IND, 0}, NULL }, // 0x33
  { ARC_ld, AAZZXD_23_15, {fA32, fBC_IND, 0}, NULL }, // 0x34
  { ARC_ld, AAZZXD_23_15, {fA32, fBC_IND, 0}, NULL }, // 0x35
  { ARC_ld, AAZZXD_23_15, {fA32, fBC_IND, 0}, NULL }, // 0x36
  { ARC_ld, AAZZXD_23_15, {fA32, fBC_IND, 0}, NULL }, // 0x37
  { 0 },                                       // 0x38
  { 0 },                                       // 0x39
  { 0 },                                       // 0x3A
  { 0 },                                       // 0x3B
  { 0 },                                       // 0x3C
  { 0 },                                       // 0x3D
  { 0 },                                       // 0x3E
  { 0 },                                       // 0x3F
};

// indexed by bits 14..12 & 26..24 (maj = 5, 21..16=0x2F, 5..0=0x3F)
static const struct arcompact_opcode_t arcompact_zop5[0x40] = {
  { 0 },                                  // 0x00
  { 0 },                                  // 0x01
  { 0 },                                  // 0x02
  { 0 },                                  // 0x03
  { 0 },                                  // 0x04
  { 0 },                                  // 0x05
  { 0 },                                  // 0x06
  { 0 },                                  // 0x07
  { 0 },                                  // 0x08
  { 0 },                                  // 0x09
  { 0 },                                  // 0x0A
  { 0 },                                  // 0x0B
  { 0 },                                  // 0x0C
  { 0 },                                  // 0x0D
  { 0 },                                  // 0x0E
  { 0 },                                  // 0x0F
  { 0 },                                  // 0x20
  { 0 },                                  // 0x21
  { 0 },                                  // 0x22
  { 0 },                                  // 0x23
  { 0 },                                  // 0x24
  { 0 },                                  // 0x25
  { 0 },                                  // 0x26
  { 0 },                                  // 0x27
  { 0 },                                  // 0x28
  { 0 },                                  // 0x29
  { 0 },                                  // 0x2A
  { 0 },                                  // 0x2B
  { 0 },                                  // 0x2C
  { 0 },                                  // 0x2D
  { 0 },                                  // 0x2E
  { 0 },                                  // 0x2F
  { 0 },                                  // 0x30
  { 0 },                                  // 0x31
  { 0 },                                  // 0x32
  { 0 },                                  // 0x33
  { 0 },                                  // 0x34
  { 0 },                                  // 0x35
  { 0 },                                  // 0x36
  { 0 },                                  // 0x37
  { 0 },                                  // 0x38
  { 0 },                                  // 0x39
  { 0 },                                  // 0x3A
  { 0 },                                  // 0x3B
  { 0 },                                  // 0x3C
  { 0 },                                  // 0x3D
  { 0 },                                  // 0x3E
  { 0 },                                  // 0x3F
};

// indexed by bits 5..0 (maj = 5, 21..16=0x2F)
static const struct arcompact_opcode_t arcompact_sop5[0x40] = {
  { ARC_swap,  AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x00
  { ARC_norm,  AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x01
  { ARC_sat16, AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x02
  { ARC_rnd16, AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x03
  { ARC_abssw, AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x04
  { ARC_abss,  AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x05
  { ARC_negsw, AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x06
  { ARC_negs,  AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x07
  { ARC_normw, AUX_GEN, {GENB, GENC,    0}, NULL }, // 0x08
  { 0 },                                            // 0x09
  { 0 },                                            // 0x0A
  { 0 },                                            // 0x0B
  { 0 },                                            // 0x0C
  { 0 },                                            // 0x0D
  { 0 },                                            // 0x0E
  { 0 },                                            // 0x0F
  { 0 },                                            // 0x10
  { 0 },                                            // 0x11
  { 0 },                                            // 0x12
  { 0 },                                            // 0x13
  { 0 },                                            // 0x14
  { 0 },                                            // 0x15
  { 0 },                                            // 0x16
  { 0 },                                            // 0x17
  { 0 },                                            // 0x18
  { 0 },                                            // 0x19
  { 0 },                                            // 0x1A
  { 0 },                                            // 0x1B
  { 0 },                                            // 0x1C
  { 0 },                                            // 0x1D
  { 0 },                                            // 0x1E
  { 0 },                                            // 0x1F
  { 0 },                                            // 0x20
  { 0 },                                            // 0x21
  { 0 },                                            // 0x22
  { 0 },                                            // 0x23
  { 0 },                                            // 0x24
  { 0 },                                            // 0x25
  { 0 },                                            // 0x26
  { 0 },                                            // 0x27
  { 0 },                                            // 0x28
  { 0 },                                            // 0x29
  { 0 },                                            // 0x2A
  { 0 },                                            // 0x2B
  { 0 },                                            // 0x2C
  { 0 },                                            // 0x2D
  { 0 },                                            // 0x2E
  { 0 },                                            // 0x2F
  { 0 },                                            // 0x30
  { 0 },                                            // 0x31
  { 0 },                                            // 0x32
  { 0 },                                            // 0x33
  { 0 },                                            // 0x34
  { 0 },                                            // 0x35
  { 0 },                                            // 0x36
  { 0 },                                            // 0x37
  { 0 },                                            // 0x38
  { 0 },                                            // 0x39
  { 0 },                                            // 0x3A
  { 0 },                                            // 0x3B
  { 0 },                                            // 0x3C
  { 0 },                                            // 0x3D
  { 0 },                                            // 0x3E
  { SUBTABLE2(14, 12, 26, 24, arcompact_zop5)},     // 0x3F
};

// indexed by bits 21..16 (maj = 5)
static const struct arcompact_opcode_t arcompact_maj5[0x40] = {
  { ARC_asl,     AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x00
  { ARC_lsr,     AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x01
  { ARC_asr,     AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x02
  { ARC_ror,     AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x03
  { ARC_mul64,   AUX_GEN, {O_ZERO,GENB,GENC}, NULL }, // 0x04
  { ARC_mulu64,  AUX_GEN, {O_ZERO,GENB,GENC}, NULL }, // 0x05
  { ARC_adds,    AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x06
  { ARC_subs,    AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x07
  { ARC_divaw,   AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x08
  { 0 },                                      // 0x09
  { ARC_asls,    AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x0A
  { ARC_asrs,    AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x0B
  { ARC_muldw,   AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x0C
  { ARC_muludw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x0D
  { ARC_mulrdw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x0E
  { 0 },                                      // 0x0F
  { ARC_macdw,   AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x10
  { ARC_macudw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x11
  { ARC_macrdw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x12
  { 0 },                                              // 0x13
  { ARC_msubdw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x14
  { 0 },                                              // 0x15
  { 0 },                                              // 0x16
  { 0 },                                              // 0x17
  { 0 },                                              // 0x18
  { 0 },                                              // 0x19
  { 0 },                                              // 0x1A
  { 0 },                                              // 0x1B
  { 0 },                                              // 0x1C
  { 0 },                                              // 0x1D
  { 0 },                                              // 0x1E
  { 0 },                                              // 0x1F
  { 0 },                                              // 0x20
  { 0 },                                              // 0x21
  { 0 },                                              // 0x22
  { 0 },                                              // 0x23
  { 0 },                                              // 0x24
  { 0 },                                              // 0x25
  { 0 },                                              // 0x26
  { 0 },                                              // 0x27
  { ARC_addsdw,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x28
  { ARC_subsdw,  AUX_GEN, {GENA, GENB, GENC}, NULL }, // 0x29
  { 0 },                                              // 0x2A
  { 0 },                                              // 0x2B
  { 0 },                                              // 0x2C
  { 0 },                                              // 0x2D
  { 0 },                                              // 0x2E
  { SUBTABLE(5,  0, arcompact_sop5)                }, // 0x2F
  { ARC_mululw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x30
  { ARC_mullw,   AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x31
  { ARC_mulflw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x32
  { ARC_maclw,   AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x33
  { ARC_macflw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x34
  { ARC_machulw, AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x35
  { ARC_machlw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x36
  { ARC_machflw, AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x37
  { ARC_mulhlw,  AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x38
  { ARC_mulhflw, AUX_GEN, {GENB, GENC, GENC}, NULL }, // 0x39
  { 0 },                                              // 0x3A
  { 0 },                                              // 0x3B
  { 0 },                                              // 0x3C
  { 0 },                                              // 0x3D
  { 0 },                                              // 0x3E
  { 0 },                                              // 0x3F
};

// indexed by bits 4..3 (maj = 0xC)
static const struct arcompact_opcode_t arcompact_maj0C[4] = {
  { ARC_ld, 0,     { fA16, fBC16_IND, 0}, NULL }, // 0x0
  { ARC_ld, AUX_B, { fA16, fBC16_IND, 0}, NULL }, // 0x1
  { ARC_ld, AUX_W, { fA16, fBC16_IND, 0}, NULL }, // 0x2
  { ARC_add, 0,    { fA16, fB16,  fC16 }, NULL }, // 0x3
};

// indexed by bits 4..3 (maj = 0xD)
static const struct arcompact_opcode_t arcompact_maj0D[4] = {
  { ARC_add, 0, {fC16, fB16, U3 }, NULL }, // 0x00
  { ARC_sub, 0, {fC16, fB16, U3 }, NULL }, // 0x01
  { ARC_asl, 0, {fC16, fB16, U3 }, NULL }, // 0x02
  { ARC_asr, 0, {fC16, fB16, U3 }, NULL }, // 0x03
};

// indexed by bits 4..3 (maj = 0xE)
static const struct arcompact_opcode_t arcompact_maj0E[4] = {
  { ARC_add, 0, {fB16, fB16, fH16}, NULL }, // 0x00
  { ARC_mov, 0, {fB16, fH16, 0   }, NULL }, // 0x01
  { ARC_cmp, 0, {fB16, fH16, 0   }, NULL }, // 0x02
  { ARC_mov, 0, {fH16, fB16, 0   }, NULL }, // 0x03
};

// indexed by bits 10..8 (maj = 0xF, 4..0 = 0x0, 7..5=0x7)
// 01111 iii 111 00000
static const struct arcompact_opcode_t arcompact_zop16[8] = {
  { ARC_nop,          0, { 0, 0, 0},            NULL }, // 0x00
  { ARC_unimp,        0, { 0, 0, 0},            NULL }, // 0x01
  { 0 },                                                // 0x02
  { 0 },                                                // 0x03
  { ARC_j,  AUX_CND|cEQ, {R_BLINK|O_IND, 0, 0}, NULL }, // 0x04
  { ARC_j,  AUX_CND|cNE, {R_BLINK|O_IND, 0, 0}, NULL }, // 0x05
  { ARC_j,            0, {R_BLINK|O_IND, 0, 0}, NULL }, // 0x06
  { ARC_j,        AUX_D, {R_BLINK|O_IND, 0, 0}, NULL }, // 0x07
};

// indexed by bits 7..5 (maj = 0xF, 4..0 = 0x0)
// 01111 bbb iii 00000
static const struct arcompact_opcode_t arcompact_sop16[8] = {
  { ARC_j,            0,  {fB16|O_IND, 0, 0}, NULL }, // 0x00
  { ARC_j,        AUX_D,  {fB16|O_IND, 0, 0}, NULL }, // 0x01
  { ARC_jl,           0,  {fB16|O_IND, 0, 0}, NULL }, // 0x02
  { ARC_jl,       AUX_D,  {fB16|O_IND, 0, 0}, NULL }, // 0x03
  { 0 },                                              // 0x04
  { 0 },                                              // 0x05
  { ARC_sub, AUX_CND|cNE, {fB16, fB16, fB16}, NULL }, // 0x06
  { SUBTABLE(10, 8, arcompact_zop16)               }, // 0x07
};

// indexed by bits 4..0 (maj = 0xF)
// 01111 bbb ccc iiiii
static const struct arcompact_opcode_t arcompact_maj0F[0x20] = {
  { SUBTABLE(7, 5, arcompact_sop16)        }, // 0x00
  { 0 },                                      // 0x01
  { ARC_sub,   0, {fB16, fB16, fC16}, NULL }, // 0x02
  { 0 },                                      // 0x03
  { ARC_and,   0, {fB16, fB16, fC16}, NULL }, // 0x04
  { ARC_or,    0, {fB16, fB16, fC16}, NULL }, // 0x05
  { ARC_bic,   0, {fB16, fB16, fC16}, NULL }, // 0x06
  { ARC_xor,   0, {fB16, fB16, fC16}, NULL }, // 0x05
  { 0 },                                      // 0x08
  { 0 },                                      // 0x09
  { 0 },                                      // 0x0A
  { ARC_tst,   0, {fB16, fC16, 0   }, NULL }, // 0x0B
  { ARC_mul64, 0, {fB16, fC16, 0   }, NULL }, // 0x0C
  { ARC_sexb,  0, {fB16, fC16, 0   }, NULL }, // 0x0D
  { ARC_sexw,  0, {fB16, fC16, 0   }, NULL }, // 0x0E
  { ARC_extb,  0, {fB16, fC16, 0   }, NULL }, // 0x0F
  { ARC_extw,  0, {fB16, fC16, 0   }, NULL }, // 0x10
  { ARC_abs,   0, {fB16, fC16, 0   }, NULL }, // 0x11
  { ARC_not,   0, {fB16, fC16, 0   }, NULL }, // 0x12
  { ARC_neg,   0, {fB16, fC16, 0   }, NULL }, // 0x13
  { ARC_add1,  0, {fB16, fB16, fC16}, NULL }, // 0x14
  { ARC_add2,  0, {fB16, fB16, fC16}, NULL }, // 0x15
  { ARC_add3,  0, {fB16, fB16, fC16}, NULL }, // 0x16
  { 0 },                                      // 0x17
  { ARC_asl,   0, {fB16, fB16, fC16}, NULL }, // 0x18
  { ARC_lsr,   0, {fB16, fB16, fC16}, NULL }, // 0x19
  { ARC_asr,   0, {fB16, fB16, fC16}, NULL }, // 0x1A
  { ARC_asl,   0, {fB16, fC16, 0   }, NULL }, // 0x1B
  { ARC_asr,   0, {fB16, fC16, 0   }, NULL }, // 0x1C
  { ARC_lsr,   0, {fB16, fC16, 0   }, NULL }, // 0x1D
  { ARC_trap,  0, {   0,    0, 0   }, NULL }, // 0x1E
  { ARC_brk,   0, {   0,    0, 0   }, NULL }, // 0x1F
};

// indexed by bits 7..5 (maj = 0x17)
static const struct arcompact_opcode_t arcompact_maj17[8] = {
  { ARC_asl,   0, {fB16, fB16, U5}, NULL }, // 0x00
  { ARC_lsr,   0, {fB16, fB16, U5}, NULL }, // 0x01
  { ARC_asr,   0, {fB16, fB16, U5}, NULL }, // 0x02
  { ARC_sub,   0, {fB16, fB16, U5}, NULL }, // 0x03
  { ARC_bset,  0, {fB16, fB16, U5}, NULL }, // 0x04
  { ARC_bclr,  0, {fB16, fB16, U5}, NULL }, // 0x05
  { ARC_bmsk,  0, {fB16, fB16, U5}, NULL }, // 0x06
  { ARC_btst,  0, {fB16,   U5,  0}, NULL }, // 0x07
};

// indexed by bits 10..8 (maj = 0x18, i=5)
static const struct arcompact_opcode_t arcompact_sp_addsub[8] = {
  { ARC_add, 0,     {R_SP, R_SP, U7L }, NULL }, // 0x00
  { ARC_sub, 0,     {R_SP, R_SP, U7L }, NULL }, // 0x01
  { 0 },                                        // 0x02
  { 0 },                                        // 0x03
  { 0 },                                        // 0x04
  { 0 },                                        // 0x05
  { 0 },                                        // 0x06
  { 0 },                                        // 0x07
};

// indexed by bits 10..8 (maj = 0x18, i=6)
static const struct arcompact_opcode_t arcompact_sp_pops[0x20] = {
  { 0 },                                         // 0x00
  { ARC_pop, 0,     {fB16, 0, 0 }, NULL },       // 0x01
  { 0 },                                         // 0x02
  { 0 },                                         // 0x03
  { 0 },                                         // 0x04
  { 0 },                                         // 0x05
  { 0 },                                         // 0x06
  { 0 },                                         // 0x07
  { 0 },                                         // 0x08
  { 0 },                                         // 0x09
  { 0 },                                         // 0x0A
  { 0 },                                         // 0x0B
  { 0 },                                         // 0x0C
  { 0 },                                         // 0x0D
  { 0 },                                         // 0x0E
  { 0 },                                         // 0x0F
  { 0 },                                         // 0x10
  { ARC_pop, 0,     {R_BLINK, 0, 0 }, NULL },    // 0x11
  { 0 },                                         // 0x12
  { 0 },                                         // 0x13
  { 0 },                                         // 0x14
  { 0 },                                         // 0x15
  { 0 },                                         // 0x16
  { 0 },                                         // 0x17
  { 0 },                                         // 0x18
  { 0 },                                         // 0x19
  { 0 },                                         // 0x1A
  { 0 },                                         // 0x1B
  { 0 },                                         // 0x1C
  { 0 },                                         // 0x1D
  { 0 },                                         // 0x1E
  { 0 },                                         // 0x1F
};

// indexed by bits 10..8 (maj = 0x18, i=7)
static const struct arcompact_opcode_t arcompact_sp_pushs[0x20] = {
  { 0 },                                         // 0x00
  { ARC_push, 0,     {fB16, 0, 0 }, NULL },      // 0x01
  { 0 },                                         // 0x02
  { 0 },                                         // 0x03
  { 0 },                                         // 0x04
  { 0 },                                         // 0x05
  { 0 },                                         // 0x06
  { 0 },                                         // 0x07
  { 0 },                                         // 0x08
  { 0 },                                         // 0x09
  { 0 },                                         // 0x0A
  { 0 },                                         // 0x0B
  { 0 },                                         // 0x0C
  { 0 },                                         // 0x0D
  { 0 },                                         // 0x0E
  { 0 },                                         // 0x0F
  { 0 },                                         // 0x10
  { ARC_push, 0,    {R_BLINK, 0, 0 }, NULL },    // 0x11
  { 0 },                                         // 0x12
  { 0 },                                         // 0x13
  { 0 },                                         // 0x14
  { 0 },                                         // 0x15
  { 0 },                                         // 0x16
  { 0 },                                         // 0x17
  { 0 },                                         // 0x18
  { 0 },                                         // 0x19
  { 0 },                                         // 0x1A
  { 0 },                                         // 0x1B
  { 0 },                                         // 0x1C
  { 0 },                                         // 0x1D
  { 0 },                                         // 0x1E
  { 0 },                                         // 0x1F
};

// indexed by bits 7..5 (maj = 0x18)
// sp-based instructions
static const struct arcompact_opcode_t arcompact_maj18[8] = {
  { ARC_ld,  0,     {fB16, SP_U7,    0 }, NULL }, // 0x00
  { ARC_ld,  AUX_B, {fB16, SP_U7,    0 }, NULL }, // 0x01
  { ARC_st,  0,     {fB16, SP_U7,    0 }, NULL }, // 0x02
  { ARC_st,  AUX_B, {fB16, SP_U7,    0 }, NULL }, // 0x03
  { ARC_add, 0,     {fB16, R_SP,   U7L }, NULL }, // 0x04
  { SUBTABLE( 10, 8, arcompact_sp_addsub)       }, // 0x05
  { SUBTABLE(  4, 0, arcompact_sp_pops)         }, // 0x06
  { SUBTABLE(  4, 0, arcompact_sp_pushs)        }, // 0x07
};

// indexed by bits 10..9 (maj = 0x19)
// gp-based ld/add (data aligned offset)
static const struct arcompact_opcode_t arcompact_maj19[4] = {
  { ARC_ld,  0,     {R_R0, GP_S11,   0 }, NULL }, // 0x00
  { ARC_ld,  AUX_B, {R_R0, GP_S9,    0 }, NULL }, // 0x01
  { ARC_ld,  AUX_W, {R_R0, GP_S10,   0 }, NULL }, // 0x02
  { ARC_add, 0,     {R_R0, R_GP,   S11 }, NULL }, // 0x03
};

// indexed by bits 7..7 (maj = 0x1C)
static const struct arcompact_opcode_t arcompact_maj1C[2] = {
  { ARC_add, 0, { fB16, fB16, U7}, NULL }, // 0x00
  { ARC_cmp, 0, { fB16, U7,    0}, NULL }, // 0x01
};

// indexed by bits 7..7 (maj = 0x1D)
static const struct arcompact_opcode_t arcompact_maj1D[2] = {
  { ARC_br, AUX_CND|cEQ, { fB16, O_ZERO, S8}, NULL }, // 0x00
  { ARC_br, AUX_CND|cNE, { fB16, O_ZERO, S8}, NULL }, // 0x01
};

// indexed by bits 8..6 (maj = 0x1E, 10..9=0x3)
static const struct arcompact_opcode_t arcompact_bcc16[8] = {
  { ARC_b,  AUX_CND|cGT, { S7, 0, 0}, NULL }, // 0x00
  { ARC_b,  AUX_CND|cGE, { S7, 0, 0}, NULL }, // 0x01
  { ARC_b,  AUX_CND|cLT, { S7, 0, 0}, NULL }, // 0x02
  { ARC_b,  AUX_CND|cLE, { S7, 0, 0}, NULL }, // 0x03
  { ARC_b,  AUX_CND|cHI, { S7, 0, 0}, NULL }, // 0x04
  { ARC_b,  AUX_CND|cHS, { S7, 0, 0}, NULL }, // 0x05
  { ARC_b,  AUX_CND|cLO, { S7, 0, 0}, NULL }, // 0x06
  { ARC_b,  AUX_CND|cLS, { S7, 0, 0}, NULL }, // 0x07
};

// indexed by bits 10..9 (maj = 0x1E)
static const struct arcompact_opcode_t arcompact_maj1E[4] = {
  { ARC_b,            0, { S10, 0, 0}, NULL }, // 0x00
  { ARC_b,  AUX_CND|cEQ, { S10, 0, 0}, NULL }, // 0x01
  { ARC_b,  AUX_CND|cNE, { S10, 0, 0}, NULL }, // 0x02
  { SUBTABLE(  8, 6, arcompact_bcc16)       }, // 0x03
};

// indexed by major opcode (bits 15..11)
static const struct arcompact_opcode_t arcompact_major[0x20] = {
  { SUBTABLE(16, 16, arcompact_maj0) },          // 0x00
  { SUBTABLE(16, 16, arcompact_maj1) },          // 0x01
  { ARC_ld, DAAZZX_11_6, {fA32, fB_S9, 0}, NULL},// 0x02
  { ARC_st, DAAZZR_5_0,  {fC32, fB_S9, 0}, NULL},// 0x03
  { SUBTABLE(21, 16, arcompact_maj4) },          // 0x04
  { SUBTABLE(21, 16, arcompact_maj5) },          // 0x05
  { 0 },                                         // 0x06
  { 0 },                                         // 0x07
  { 0 },                                         // 0x08
  { 0 },                                         // 0x09
  { 0 },                                         // 0x0A
  { 0 },                                         // 0x0B
  { SUBTABLE( 4,  3, arcompact_maj0C) },         // 0x0C
  { SUBTABLE( 4,  3, arcompact_maj0D) },         // 0x0D
  { SUBTABLE( 4,  3, arcompact_maj0E) },         // 0x0E
  { SUBTABLE( 4,  0, arcompact_maj0F) },         // 0x0F
  { ARC_ld, 0,     { fC16, fB_U7, 0}, NULL },      // 0x10
  { ARC_ld, AUX_B, { fC16, fB_U5, 0}, NULL },      // 0x11
  { ARC_ld, AUX_W, { fC16, fB_U6, 0}, NULL },      // 0x12
  { ARC_ld, AUX_W|AUX_X, { fC16, fB_U6, 0}, NULL },// 0x13
  { ARC_st, 0,     { fC16, fB_U7, 0}, NULL },      // 0x14
  { ARC_st, AUX_B, { fC16, fB_U5, 0}, NULL },      // 0x15
  { ARC_st, AUX_W, { fC16, fB_U6, 0}, NULL },      // 0x16
  { SUBTABLE( 7,  5, arcompact_maj17) },         // 0x17
  { SUBTABLE( 7,  5, arcompact_maj18) },         // 0x18
  { SUBTABLE(10,  9, arcompact_maj19) },         // 0x19
  { ARC_ld,  0, { fB16, PCL_U10, 0}, NULL },     // 0x1A
  { ARC_mov, 0, { fB16, U8, 0}, NULL },          // 0x1B
  { SUBTABLE( 7,  7, arcompact_maj1C) },         // 0x1C
  { SUBTABLE( 7,  7, arcompact_maj1D) },         // 0x1D
  { SUBTABLE(10,  9, arcompact_maj1E) },         // 0x1E
  { ARC_bl, 0, { S13, 0, 0}, NULL },             // 0x1F
};

// extract bit numbers high..low from val (inclusive, start from 0)
#define BITS(val, high, low) ( ((val)>>low) & ( (1<<(high-low+1))-1) )
// sign extend b low bits in x
// from "Bit Twiddling Hacks"
static sval_t SIGNEXT(sval_t x, int b)
{
  uint32 m = 1 << (b - 1);
  x &= ((1 << b) - 1);
  return (x ^ m) - m;
}

// extract bitfield with sign extension
#define SBITS(val, high, low) SIGNEXT(BITS(val, high, low), high-low+1)

//----------------------------------------------------------------------
bool got_limm;
static int get_limm()
{
  static int g_limm;
  if ( !got_limm )
  {
    g_limm  = (ua_next_word() << 16);
    g_limm |= ua_next_word();
    got_limm = true;
  }
  return g_limm;
}

//----------------------------------------------------------------------
// register, or a reference to long immediate (r62)
inline void opreg(op_t &x, int rgnum)
{
  if ( rgnum != LIMM )
  {
    x.reg  = uint16(rgnum);
    x.type = o_reg;
  }
  else
  {
    x.type = o_imm;
    // limm as destination is not used
    // so check for instructions where first operand is source
    if ( x.n == 0 && (cmd.get_canon_feature() & CF_CHG1) != 0 )
      x.value = 0;
    else
      x.value = get_limm();
  }
  x.dtyp = dt_dword;
}

//----------------------------------------------------------------------
inline void opimm(op_t &x, uval_t val)
{
  x.value = val;
  x.type  = o_imm;
  x.dtyp  = dt_dword;
}

//----------------------------------------------------------------------
inline void opdisp(op_t &x, int rgnum, ea_t disp)
{
  if ( rgnum != LIMM )
  {
    x.type  = o_displ;
    x.addr  = disp;
    x.reg   = rgnum;
  }
  else
  {
    x.type = o_mem;
    x.addr = get_limm() + disp;
  }
  x.dtyp = dt_dword;
}

//----------------------------------------------------------------------
inline int reg16(int rgnum)
{
  // 0..3 r0-r3
  // 4..7 r12-r15
  return ( rgnum > 3 ) ? (rgnum + 8) : rgnum;
}

//----------------------------------------------------------------------
inline void opbranch(op_t &x, sval_t delta)
{
  // cPC <- (cPCL+delta)
  // PCL is current instruction address with 2 low bits set to 0
  ea_t pcl = cmd.ip & ~3ul;
  x.type = o_near;
  x.dtyp = dt_code;
  x.addr = pcl + delta;
}

//----------------------------------------------------------------------
static void decode_operand(uint32 code, op_t &x, uint32 opkind)
{
  if ( opkind == 0 )
  {
    x.type = o_void;
    return;
  }
  int reg, p;
  sval_t displ;
  switch ( opkind & ~O_IND )
  {
    case fA16:
      opreg(x, reg16(BITS(code, 2, 0)));
      break;
    case fB16:
      opreg(x, reg16(BITS(code, 10, 8)));
      break;
    case fC16:
      opreg(x, reg16(BITS(code, 7, 5)));
      break;

    case fA32:    //  5..0                   a register operand (6 bits, r0-r63)
      opreg(x, BITS(code, 5, 0));
      break;
    case fB32:    // 14..12 & 26..24         b register operand (6 bits)
      opreg(x, (BITS(code, 14, 12)<<3) | BITS(code, 26, 24));
      break;
    case fC32:    // 11..6                   c register operand (6 bits)
      opreg(x, BITS(code, 11, 6));
      break;

    case fH16:  //  2..0 & 7..5            h register operand (6 bits)
      reg = (BITS(code, 2, 0) << 3) | BITS(code, 7, 5);
      opreg(x, reg);
      break;
    case S25L:           // 15..6 & 26..18 & 0..3 s25 signed branch displacement for branch and link
    case S21L:           // 15..6 & 26..18        s21 signed branch displacement for branch and link
    case S25:            // 15..6 & 26..17 & 3..0 s25 signed branch displacement
    case S21:            // 15..6 & 26..17        s21 signed branch displacement
      displ = (BITS(code, 15, 6) << 10) | BITS(code, 26, 17);
      if ( opkind == S25 || opkind == S25L )
      {
        displ |= BITS(code, 3, 0) << 20;
        if ( displ & (1ul<<23) )
          displ -= (1ul<<24);
      }
      else
      {
        if ( displ & (1ul<<19) )
          displ -= (1ul<<20);
      }
      if ( opkind == S25L || opkind == S21L )
      {
        // branch-and-link uses 32-bit aligned target
        displ &= ~1ul;
      }
      opbranch(x, displ * 2);
      break;

    case S9:              // 15&23..17             s9 signed branch displacement (16-bit aligned)
      displ = BITS(code, 23, 17);
      if ( BITS(code, 15, 15) ) // sign bit
        displ -= (1ul<<7);
      opbranch(x, displ * 2);
      break;

    case S7:              // 5..0                  s7 signed branch displacement (16-bit aligned)
      displ = SBITS(code, 5, 0);
      opbranch(x, displ * 2);
      break;

    case S8:              // 6..0                  s8 signed branch displacement (16-bit aligned)
      displ = SBITS(code, 6, 0);
      opbranch(x, displ * 2);
      break;

    case S10:             // 8..0                  s10 signed branch displacement (16-bit aligned)
      displ = SBITS(code, 8, 0);
      opbranch(x, displ * 2);
      break;

    case S13:             // 10..0                 s13 signed branch displacement (32-bit aligned)
      displ = SBITS(code, 10, 0);
      opbranch(x, displ * 4);
      break;

    case PCL_U10:
      displ = BITS(code, 7, 0);
      opdisp(x, PCL, displ*4);
      break;

    case SP_U7: //  4..0                 [SP, u7]   stack + offset (u7 = u5*4)
      displ = BITS(code, 4, 0);
      opdisp(x, SP, displ*4);
      break;

    case U3:             //  2..0                  u2 unsigned immediate
      opimm(x, BITS(code, 2, 0));
      break;

    case U7:
      opimm(x, BITS(code, 6, 0));
      break;

    case U6:
      opimm(x, BITS(code, 11, 6));
      break;

    case U5:
    case U7L:
      displ = BITS(code, 4, 0);
      if ( opkind == U7L )
        displ *= 4;
      opimm(x, displ);
      break;

    case U8:
      opimm(x, BITS(code, 7, 0));
      break;

    case fB_U5:          //  10..8 & 4..0         [b, u5]
    case fB_U6:          //  10..8 & 4..0         [b, u6] (u6=u5*2)
    case fB_U7:          //  10..8 & 4..0         [b, u7] (u6=u5*4)
      displ = BITS(code, 4, 0);
      if ( opkind == fB_U6 )
        displ *= 2;
      else if ( opkind == fB_U7 )
        displ *= 4;
      reg = reg16(BITS(code, 10, 8));
      opdisp(x, reg, displ);
      break;

    case fB_S9:          //  14..12&26..26, 15&23..16   [b, s9]
      displ = BITS(code, 23, 16);
      if ( BITS(code, 15, 15) ) // sign bit
        displ -= (1ul<<8);
      reg = (BITS(code, 14, 12)<<3) | BITS(code, 26, 24);
      opdisp(x, reg, displ);
      break;

    // handing of the "gen" format:
    //                 P   M
    //  REG_REG        00 N/A Destination and both sources are registers
    //  REG_U6IMM      01 N/A Source 2 is a 6-bit unsigned immediate
    //  REG_S12IMM     10 N/A Source 2 is a 12-bit signed immediate
    //  COND_REG       11  0  Conditional instruction. Destination (if any) is source 1. Source 2 is a register
    //  COND_REG_U6IMM 11  1  Conditional instruction. Destination (if any) is source 1. Source 2 is a 6-bit unsigned immediate
    //    P=23..22, M=5
    //  0x04, [0x00 - 0x3F]
    //   00100 bbb 00 iiiiii F BBB CCCCCC AAAAAA   reg-reg      op<.f> a,b,c
    //   00100 bbb 01 iiiiii F BBB UUUUUU AAAAAA   reg-u6imm    op<.f> a,b,u6
    //   00100 bbb 10 iiiiii F BBB ssssss SSSSSS   reg-s12imm   op<.f> b,b,s12
    //   00100 bbb 11 iiiiii F BBB CCCCCC 0 QQQQQ  cond reg-reg op<.cc><.f> b,b,c
    //   00100 bbb 11 iiiiii F BBB UUUUUU 1 QQQQQ  cond reg-u6  op<.cc><.f> b,b,u6
    //  0x04, [0x30 - 0x37]
    //   00100 bbb aa 110 ZZ X D BBB CCCCCC AAAAAA LD<zz><.x><.aa><.di> a,[b,c]

    case GENA:    //  5..0
      p = BITS(code, 23, 22);
      if ( p <= 1 )
        reg = BITS(code, 5, 0);
      else
        reg = (BITS(code, 14, 12)<<3) | BITS(code, 26, 24);
      opreg(x, reg);
      break;

    case GENB:    // 14..12 & 26..24
      reg = (BITS(code, 14, 12)<<3) | BITS(code, 26, 24);
      opreg(x, reg);
      break;

    case GENC:       // 11..6 reg/u6 or 0..5&11..6 s12
    case GENC_PCREL: // 11..6 u6 or 0..5&11..6 s12 pc-relative displacement
      p = BITS(code, 23, 22);
      if ( p != 2 )
      {
        reg = BITS(code, 11, 6);
        if ( p == 0 || (p == 3 && BITS(code, 5, 5) == 0) )
          opreg(x, reg);
        else
          opimm(x, reg);
      }
      else
      {
        // s12
        reg = (BITS(code, 5, 0) << 6) | BITS(code, 11, 6);
        reg = SIGNEXT(reg, 12);
        opimm(x, reg);
      }
      if ( (opkind & ~O_IND) == GENC_PCREL && x.type == o_imm )
        opbranch(x, reg * 2);
      break;

    case fBC_IND:
      {
        int b = (BITS(code, 14, 12)<<3) | BITS(code, 26, 24);
        int c = BITS(code, 11, 6);
        int li = 0;
        if ( b == LIMM || c == LIMM )
          li = get_limm();
        doIndirectOperand(b, c, x, 0, li, false);
      }
      break;

    case fBC16_IND:
      {
        int b = BITS(code, 10, 8);
        int c = BITS(code,  7, 5);
        doIndirectOperand(reg16(b), reg16(c), x, 0, 0, false);
      }
      break;

    case O_ZERO:
      opimm(x, 0);
      break;

    case R_SP:           // implicit SP
      opreg(x, SP);
      break;

    case R_BLINK:        // implicit BLINK
      opreg(x, BLINK);
      break;

    case R_R0:           // implicit R0
      opreg(x, R0);
      break;

    case R_GP:           // implicit GP
      opreg(x, GP);
      break;

    case GP_S9:          //  8..0  [GP, s9]   GP + offset
    case GP_S10:         //  8..0  [GP, s10]  GP + offset (s10 = s9*2)
    case GP_S11:         //  8..0  [GP, s11]  GP + offset (s11 = s9*4)
    case S11:            //  8..0  s11 signed immediate (s11 = s9*4)
      displ = SBITS(code, 8, 0);
      if ( opkind == GP_S10 )
        displ *= 2;
      else if ( opkind != GP_S9 )
        displ *= 4;
      if ( opkind == S11 )
        opimm(x, displ);
      else
        opdisp(x, GP, displ);
      break;

    default:
      msg("%a: cannot decode operand %d (opkind=%u)\n", cmd.ea, x.n, opkind);
      return;
  }
  if ( opkind & O_IND )
  {
    // indirect access
    if ( x.type == o_reg )
    {
      x.type = o_displ;
      x.addr = 0;
    }
    else if ( x.type == o_imm )
    {
      if ( cmd.itype == ARC_j || cmd.itype == ARC_jl )
        x.type = o_near;
      else
        x.type = o_mem;
      x.addr = x.value;
    }
  }
}

//----------------------------------------------------------------------
// decode non-operand bits of the instruction
static void decode_aux(uint32 code, uint32 aux)
{
  if ( aux & AUX_CND )
  {
    // condition in low bits of 'aux'
    cmd.auxpref = (cmd.auxpref & ~aux_cmask) | (aux & aux_cmask);
    aux &= ~(AUX_CND | aux_cmask);
  }
  if ( aux & Q_4_0 )
  {
    // condition in low bits of instruction
    cmd.auxpref = (cmd.auxpref & ~aux_cmask) | (code & aux_cmask);
    aux &= ~Q_4_0;
  }
  if ( aux & (AUX_GEN|AUX_GEN2) )
  {
    // bit 15 = F, 4..0 = Q if 23..22=0x3
    if ( (aux & AUX_GEN2) == 0 && BITS(code, 15, 15) )
      cmd.auxpref |= aux_f;
    if ( BITS(code, 23, 22) == 3 )
      cmd.auxpref = (cmd.auxpref & ~aux_cmask) | (code & aux_cmask);
    aux &= ~(AUX_GEN|AUX_GEN2);
  }
  if ( aux & N_5 )
  {
    cmd.auxpref = (cmd.auxpref & ~aux_d) | (code & aux_d);
    aux &= ~N_5;
  }
  if ( aux & AUX_W )
  {
    cmd.auxpref = (cmd.auxpref & ~aux_zmask) | aux_w;
    aux &= ~AUX_W;
  }
  if ( aux & AUX_B )
  {
    cmd.auxpref = (cmd.auxpref & ~aux_zmask) | aux_b;
    aux &= ~AUX_B;
  }
  if ( aux & AUX_X )
  {
    cmd.auxpref |= aux_x;
    aux &= ~AUX_X;
  }
  if ( aux & AUX_D )
  {
    cmd.auxpref = (cmd.auxpref & ~aux_nmask) | aux_d;
    aux &= ~AUX_D;
  }
  if ( aux & DAAZZX_11_6 ) // 11..6   Di, aa, ZZ, X flags (load)
  {
    cmd.auxpref = (cmd.auxpref & ~0x3F) | (BITS(code, 11, 6));
    aux &= ~DAAZZX_11_6;
  }
  if ( aux & DAAZZR_5_0 ) //  5..0   Di, aa, ZZ, R flags (store)
  {
    cmd.auxpref = (cmd.auxpref & ~0x3F) | (BITS(code, 5, 0));
    aux &= ~DAAZZR_5_0;
  }
  if ( aux & AAZZXD_23_15 ) //  23..22,18..15  aa, ZZ, X, D flags (load reg+reg)
  {
    // load instructions flags: Di.AA.ZZ.X
    cmd.auxpref &= ~0x3F;
    cmd.auxpref |= BITS(code, 15, 15) << 5; // Di
    cmd.auxpref |= BITS(code, 23, 22) << 3; // aa
    cmd.auxpref |= BITS(code, 18, 17) << 1; // ZZ
    cmd.auxpref |= BITS(code, 16, 16) << 0; // X
    aux &= ~AAZZXD_23_15;
  }
  if ( aux != 0 )
    msg("%a: unhandled aux bits: %08X\n", cmd.ea, aux);
}

//----------------------------------------------------------------------
static int analyze_compact(uint32 code, int idx, const struct arcompact_opcode_t *table)
{
  const arcompact_opcode_t *line = &table[idx];
  while ( (line->mnem & 0x80000000) != 0 )
  {
    // it's a pointer into subtable
    // indexed by some of the instruction's bits
    int high1 = (line->mnem >> 24) & 0x1F;
    int low1  = (line->mnem >> 16) & 0x1F;
    int high2 = (line->mnem >>  8) & 0x1F;
    int low2  = (line->mnem >>  0) & 0x1F;
    idx = BITS(code, high2, low2);
    if ( high1 != 0 && low1 != 0 )
      idx |= BITS(code, high1, low1) << (high2-low2+1);
    line = &(line->subtable[idx]);
  }
  if ( line->mnem == 0 )
  {
    return 0;
  }

  cmd.itype = line->mnem;
  decode_aux(code, line->aux);
  for ( int i = 0; i < 3; i++ )
    decode_operand(code, cmd.Operands[i], line->ops[i]);
  return cmd.size;
}

//----------------------------------------------------------------------
// analyze ARCompact instruction
static int ana_compact(void)
{
  // must be 16-bit aligned
  if ( cmd.ea & 1 )
    return 0;
  uint32 code = ua_next_word();
  got_limm = false;
  // first 5 bits is the major opcode
  int i = (code >> 11) & 0x1F;
  if ( i < 0xC )
  {
    // this is a 32-bit instruction
    // get the full word
    code = (code << 16) | ua_next_word();
  }
  return analyze_compact(code, i, arcompact_major);
}

//----------------------------------------------------------------------
static void simplify()
{
  switch ( cmd.itype )
  {
    case ARC_st:
    case ARC_ld:
      // ld.as r1, [r2, delta] -> ld r1, [r2, delta*size]
      if ( cmd.Op2.type == o_displ && (cmd.auxpref & aux_amask) == aux_as && cmd.Op2.membase == 0 )
      {
        int mul = 4;
        if ( (cmd.auxpref & aux_zmask) == aux_w )
          mul = 2;
        else if ( (cmd.auxpref & aux_zmask) != aux_l )
          break;
        cmd.Op2.addr *= mul;
        cmd.auxpref &= ~aux_amask;
      }
      break;
    case ARC_add1:
    case ARC_add2:
    case ARC_add3:
    case ARC_sub1:
    case ARC_sub2:
    case ARC_sub3:
      // addN a, b, c -> add a, b, c<<N
      if ( cmd.Op3.type == o_imm )
      {
        switch ( cmd.itype )
        {
          case ARC_add1:
          case ARC_sub1:
            cmd.Op3.value *= 2;
            break;
          case ARC_add2:
          case ARC_sub2:
            cmd.Op3.value *= 4;
            break;
          case ARC_add3:
          case ARC_sub3:
            cmd.Op3.value *= 8;
            break;
        }
        switch ( cmd.itype )
        {
          case ARC_add1:
          case ARC_add2:
          case ARC_add3:
            cmd.itype = ARC_add;
            break;
          case ARC_sub3:
          case ARC_sub2:
          case ARC_sub1:
            cmd.itype = ARC_sub;
            break;
        }
      }
      break;
    case ARC_sub:
      // sub.f   0, a, b -> cmp a, b
      if ( cmd.Op1.is_imm(0) && (cmd.auxpref & aux_f) != 0 )
      {
        cmd.auxpref &= ~aux_f;
        cmd.itype = ARC_cmp;
        cmd.Op1 = cmd.Op2;
        cmd.Op2 = cmd.Op3;
        cmd.Op3.type = o_void;
      }
      break;
  }
}

//----------------------------------------------------------------------
// fix operand size for byte or word loads/stores
inline void fix_ldst()
{
  if ( cmd.itype == ARC_ld || cmd.itype == ARC_st )
  {
    switch ( cmd.auxpref & aux_zmask )
    {
      case aux_b:
        cmd.Op2.dtyp = dt_byte;
        break;
      case aux_w:
        cmd.Op2.dtyp = dt_word;
        break;
    }
  }
}

//----------------------------------------------------------------------
// convert pc-relative loads
// ld r1, [pc,#delta] -> ld r1, [memaddr]
static void inline_const()
{
  if ( cmd.itype == ARC_ld
    && cmd.Op2.type == o_displ
    && cmd.Op2.reg == PCL
    && (cmd.auxpref & (aux_a|aux_zmask)) == 0 ) // no .a and 32-bit access
  {
    ea_t val_ea = (cmd.ea & ~3ul) + cmd.Op2.addr;
    if ( isEnabled(val_ea) )
    {
      cmd.Op2.type = o_mem;
      cmd.Op2.addr = val_ea;
      cmd.auxpref |= aux_pcload;
    }
  }
}

//----------------------------------------------------------------------
// analyze an instruction
int idaapi ana(void)
{
  int sz = is_a4() ? ana_old() : ana_compact();
  if ( sz != 0 )
  {
    fix_ldst();
    if ( (idpflags & ARC_SIMPLIFY) != 0 )
      simplify();
    if ( (idpflags & ARC_INLINECONST) != 0 )
      inline_const();
  }
  return cmd.size;
}
