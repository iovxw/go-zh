// Derived from Inferno utils/5c/swt.c
// http://code.google.com/p/inferno-os/source/browse/utils/5c/swt.c
//
//	Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
//	Portions Copyright © 1995-1997 C H Forsyth (forsyth@terzarima.net)
//	Portions Copyright © 1997-1999 Vita Nuova Limited
//	Portions Copyright © 2000-2007 Vita Nuova Holdings Limited (www.vitanuova.com)
//	Portions Copyright © 2004,2006 Bruce Ellis
//	Portions Copyright © 2005-2007 C H Forsyth (forsyth@terzarima.net)
//	Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
//	Portions Copyright © 2009 The Go Authors.  All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <link.h>
#include "../cmd/5l/5.out.h"
#include "../pkg/runtime/stack.h"

static Addr noaddr = {
	.type = D_NONE,
	.name = D_NONE,
	.reg = NREG,
};

static Prog zprg = {
	.as = AGOK,
	.scond = 14,
	.reg = NREG,
	.from = {
		.name = D_NONE,
		.type = D_NONE,
		.reg = NREG,
	},
	.to = {
		.name = D_NONE,
		.type = D_NONE,
		.reg = NREG,
	},
};

static void
zname(Biobuf *b, LSym *s, int t)
{
	BPUTC(b, ANAME);	/* as */
	BPUTC(b, t);		/* type */
	BPUTC(b, s->symid);	/* sym */
	Bwrite(b, s->name, strlen(s->name)+1);
}

static void
zfile(Biobuf *b, char *p, int n)
{
	BPUTC(b, ANAME);
	BPUTC(b, D_FILE);
	BPUTC(b, 1);
	BPUTC(b, '<');
	Bwrite(b, p, n);
	BPUTC(b, 0);
}

static void
zaddr(Biobuf *b, Addr *a, int s, int gotype)
{
	int32 l;
	uint64 e;
	int i;
	char *n;

	switch(a->type) {
	case D_STATIC:
	case D_AUTO:
	case D_EXTERN:
	case D_PARAM:
		// TODO(kaib): remove once everything seems to work
		sysfatal("We should no longer generate these as types");

	default:
		BPUTC(b, a->type);
		BPUTC(b, a->reg);
		BPUTC(b, s);
		BPUTC(b, a->name);
		BPUTC(b, gotype);
	}

	switch(a->type) {
	default:
		print("unknown type %d in zaddr\n", a->type);

	case D_NONE:
	case D_REG:
	case D_FREG:
	case D_PSR:
		break;

	case D_CONST2:
		l = a->offset2;
		BPUTLE4(b, l); // fall through
	case D_OREG:
	case D_CONST:
	case D_SHIFT:
	case D_STATIC:
	case D_AUTO:
	case D_EXTERN:
	case D_PARAM:
		l = a->offset;
		BPUTLE4(b, l);
		break;

	case D_BRANCH:
		if(a->offset == 0 || a->u.branch != nil) {
			if(a->u.branch == nil)
				sysfatal("unpatched branch %D", a);
			a->offset = a->u.branch->loc;
		}
		l = a->offset;
		BPUTLE4(b, l);
		break;

	case D_SCONST:
		n = a->u.sval;
		for(i=0; i<NSNAME; i++) {
			BPUTC(b, *n);
			n++;
		}
		break;

	case D_REGREG:
	case D_REGREG2:
		BPUTC(b, a->offset);
		break;

	case D_FCONST:
		double2ieee(&e, a->u.dval);
		BPUTLE4(b, e);
		BPUTLE4(b, e >> 32);
		break;
	}
}

static void
zhist(Biobuf *b, int line, vlong offset)
{
	Addr a;

	BPUTC(b, AHISTORY);
	BPUTC(b, C_SCOND_NONE);
	BPUTC(b, NREG);
	BPUTLE4(b, line);
	zaddr(b, &noaddr, 0, 0);
	a = noaddr;
	if(offset != 0) {
		a.offset = offset;
		a.type = D_CONST;
	}
	zaddr(b, &a, 0, 0);
}

static int
symtype(Addr *a)
{
	return a->name;
}

static void
zprog(Link *ctxt, Biobuf *b, Prog *p, int sf, int gf, int st, int gt)
{
	USED(ctxt);

	BPUTC(b, p->as);
	BPUTC(b, p->scond);
 	BPUTC(b, p->reg);
	BPUTLE4(b, p->lineno);
	zaddr(b, &p->from, sf, gf);
	zaddr(b, &p->to, st, gt);
}

static int
isdata(Prog *p)
{
	return p->as == ADATA || p->as == AGLOBL;
}

static int
iscall(Prog *p)
{
	return p->as == ABL;
}

static int
datasize(Prog *p)
{
	return p->reg;
}

static int
textflag(Prog *p)
{
	return p->reg;
}

static void
settextflag(Prog *p, int f)
{
	p->reg = f;
}

static Prog*
prg(void)
{
	Prog *p;

	p = emallocz(sizeof(*p));
	*p = zprg;
	return p;
}

static	Prog*	stacksplit(Link*, Prog*, int32);
static	void		initdiv(Link*);
static	void	softfloat(Link*, LSym*);

// Prog.mark
enum
{
	FOLL = 1<<0,
	LABEL = 1<<1,
	LEAF = 1<<2,
};

static void
linkcase(Prog *casep)
{
	Prog *p;

	for(p = casep; p != nil; p = p->link){
		if(p->as == ABCASE) {
			for(; p != nil && p->as == ABCASE; p = p->link)
				p->pcrel = casep;
			break;
		}
	}
}

static void
nocache(Prog *p)
{
	p->optab = 0;
	p->from.class = 0;
	p->to.class = 0;
}

static void
addstacksplit(Link *ctxt, LSym *cursym)
{
	Prog *p, *pl, *q, *q1, *q2;
	int o;
	LSym *tlsfallback;
	int32 autosize, autoffset;
	
	autosize = 0;

	if(ctxt->symmorestack[0] == nil)
		ctxt->symmorestack[0] = linklookup(ctxt, "runtime.morestack", 0);
	
	tlsfallback = linklookup(ctxt, "runtime.read_tls_fallback", 0);
	ctxt->gmsym = nil;
	if(ctxt->linkmode == LinkExternal)
		ctxt->gmsym = linklookup(ctxt, "runtime.tlsgm", 0);
	q = nil;
	
	ctxt->cursym = cursym;

	if(cursym->text == nil || cursym->text->link == nil)
		return;				

	softfloat(ctxt, cursym);

	if(ctxt->debugzerostack) {
		p = cursym->text;
		autoffset = p->to.offset;
		if(autoffset < 0)
			autoffset = 0;
		if(autoffset && !(p->reg&NOSPLIT)) {
			// MOVW $4(R13), R1
			p = appendp(ctxt, p);
			p->as = AMOVW;
			p->from.type = D_CONST;
			p->from.reg = 13;
			p->from.offset = 4;
			p->to.type = D_REG;
			p->to.reg = 1;
	
			// MOVW $n(R13), R2
			p = appendp(ctxt, p);
			p->as = AMOVW;
			p->from.type = D_CONST;
			p->from.reg = 13;
			p->from.offset = 4 + autoffset;
			p->to.type = D_REG;
			p->to.reg = 2;
	
			// MOVW $0, R3
			p = appendp(ctxt, p);
			p->as = AMOVW;
			p->from.type = D_CONST;
			p->from.offset = 0;
			p->to.type = D_REG;
			p->to.reg = 3;
	
			// L:
			//	MOVW.nil R3, 0(R1) +4
			//	CMP R1, R2
			//	BNE L
			p = pl = appendp(ctxt, p);
			p->as = AMOVW;
			p->from.type = D_REG;
			p->from.reg = 3;
			p->to.type = D_OREG;
			p->to.reg = 1;
			p->to.offset = 4;
			p->scond |= C_PBIT;
	
			p = appendp(ctxt, p);
			p->as = ACMP;
			p->from.type = D_REG;
			p->from.reg = 1;
			p->reg = 2;
	
			p = appendp(ctxt, p);
			p->as = ABNE;
			p->to.type = D_BRANCH;
			p->pcond = pl;
		}
	}

	/*
	 * find leaf subroutines
	 * strip NOPs
	 * expand RET
	 * expand BECOME pseudo
	 * fixup TLS
	 */

	for(p = cursym->text; p != nil; p = p->link) {
		switch(p->as) {
		case ACASE:
			if(ctxt->flag_shared)
				linkcase(p);
			break;

		case ATEXT:
			p->mark |= LEAF;
			break;

		case ARET:
			break;

		case ADIV:
		case ADIVU:
		case AMOD:
		case AMODU:
			q = p;
			if(ctxt->sym_div == nil)
				initdiv(ctxt);
			cursym->text->mark &= ~LEAF;
			continue;

		case ANOP:
			q1 = p->link;
			q->link = q1;		/* q is non-nop */
			if(q1 != nil)
				q1->mark |= p->mark;
			continue;

		case ABL:
		case ABX:
			cursym->text->mark &= ~LEAF;

		case ABCASE:
		case AB:

		case ABEQ:
		case ABNE:
		case ABCS:
		case ABHS:
		case ABCC:
		case ABLO:
		case ABMI:
		case ABPL:
		case ABVS:
		case ABVC:
		case ABHI:
		case ABLS:
		case ABGE:
		case ABLT:
		case ABGT:
		case ABLE:
			q1 = p->pcond;
			if(q1 != nil) {
				while(q1->as == ANOP) {
					q1 = q1->link;
					p->pcond = q1;
				}
			}
			break;
		case AWORD:
			// Rewrite TLS register fetch: MRC 15, 0, <reg>, C13, C0, 3
			if((p->to.offset & 0xffff0fff) == 0xee1d0f70) {
				if(ctxt->headtype == Hopenbsd) {
					p->as = ARET;
				} else if(ctxt->goarm < 7) {
					if(tlsfallback->type != STEXT) {
						ctxt->diag("runtime·read_tls_fallback not defined");
						sysfatal("tlsfallback");
					}
					// BL runtime.read_tls_fallback(SB)
					p->as = ABL;
					p->to.type = D_BRANCH;
					p->to.sym = tlsfallback;
					p->pcond = tlsfallback->text;
					p->to.offset = 0;
					cursym->text->mark &= ~LEAF;
				}
				if(ctxt->linkmode == LinkExternal) {
					// runtime.tlsgm is relocated with R_ARM_TLS_LE32
					// and $runtime.tlsgm will contain the TLS offset.
					//
					// MOV $runtime.tlsgm+ctxt->tlsoffset(SB), REGTMP
					// ADD REGTMP, <reg>
					//
					// In shared mode, runtime.tlsgm is relocated with
					// R_ARM_TLS_IE32 and runtime.tlsgm(SB) will point
					// to the GOT entry containing the TLS offset.
					//
					// MOV runtime.tlsgm(SB), REGTMP
					// ADD REGTMP, <reg>
					// SUB -ctxt->tlsoffset, <reg>
					//
					// The SUB compensates for ctxt->tlsoffset
					// used in runtime.save_gm and runtime.load_gm.
					q = p;
					p = appendp(ctxt, p);
					p->as = AMOVW;
					p->scond = 14;
					p->reg = NREG;
					if(ctxt->flag_shared) {
						p->from.type = D_OREG;
						p->from.offset = 0;
					} else {
						p->from.type = D_CONST;
						p->from.offset = ctxt->tlsoffset;
					}
					p->from.sym = ctxt->gmsym;
					p->from.name = D_EXTERN;
					p->to.type = D_REG;
					p->to.reg = REGTMP;
					p->to.offset = 0;

					p = appendp(ctxt, p);
					p->as = AADD;
					p->scond = 14;
					p->reg = NREG;
					p->from.type = D_REG;
					p->from.reg = REGTMP;
					p->to.type = D_REG;
					p->to.reg = (q->to.offset & 0xf000) >> 12;
					p->to.offset = 0;

					if(ctxt->flag_shared) {
						p = appendp(ctxt, p);
						p->as = ASUB;
						p->scond = 14;
						p->reg = NREG;
						p->from.type = D_CONST;
						p->from.offset = -ctxt->tlsoffset;
						p->to.type = D_REG;
						p->to.reg = (q->to.offset & 0xf000) >> 12;
						p->to.offset = 0;
					}
				}
			}
		}
		q = p;
	}

	for(p = cursym->text; p != nil; p = p->link) {
		o = p->as;
		switch(o) {
		case ATEXT:
			autosize = p->to.offset + 4;
			if(autosize <= 4)
			if(cursym->text->mark & LEAF) {
				p->to.offset = -4;
				autosize = 0;
			}

			if(!autosize && !(cursym->text->mark & LEAF)) {
				if(ctxt->debugvlog) {
					Bprint(ctxt->bso, "save suppressed in: %s\n",
						cursym->name);
					Bflush(ctxt->bso);
				}
				cursym->text->mark |= LEAF;
			}
			if(cursym->text->mark & LEAF) {
				cursym->leaf = 1;
				if(!autosize)
					break;
			}

			if(!(p->reg & NOSPLIT))
				p = stacksplit(ctxt, p, autosize); // emit split check
			
			// MOVW.W		R14,$-autosize(SP)
			p = appendp(ctxt, p);
			p->as = AMOVW;
			p->scond |= C_WBIT;
			p->from.type = D_REG;
			p->from.reg = REGLINK;
			p->to.type = D_OREG;
			p->to.offset = -autosize;
			p->to.reg = REGSP;
			p->spadj = autosize;
			
			if(cursym->text->reg & WRAPPER) {
				// g->panicwrap += autosize;
				// MOVW panicwrap_offset(g), R3
				// ADD $autosize, R3
				// MOVW R3 panicwrap_offset(g)
				p = appendp(ctxt, p);
				p->as = AMOVW;
				p->from.type = D_OREG;
				p->from.reg = REGG;
				p->from.offset = 2*ctxt->arch->ptrsize;
				p->to.type = D_REG;
				p->to.reg = 3;
			
				p = appendp(ctxt, p);
				p->as = AADD;
				p->from.type = D_CONST;
				p->from.offset = autosize;
				p->to.type = D_REG;
				p->to.reg = 3;
				
				p = appendp(ctxt, p);
				p->as = AMOVW;
				p->from.type = D_REG;
				p->from.reg = 3;
				p->to.type = D_OREG;
				p->to.reg = REGG;
				p->to.offset = 2*ctxt->arch->ptrsize;
			}
			break;

		case ARET:
			nocache(p);
			if(cursym->text->mark & LEAF) {
				if(!autosize) {
					p->as = AB;
					p->from = zprg.from;
					if(p->to.sym) { // retjmp
						p->to.type = D_BRANCH;
						p->pcond = p->to.sym->text;
					} else {
						p->to.type = D_OREG;
						p->to.offset = 0;
						p->to.reg = REGLINK;
					}
					break;
				}
			}

			if(cursym->text->reg & WRAPPER) {
				int scond;
				
				// Preserve original RET's cond, to allow RET.EQ
				// in the implementation of reflect.call.
				scond = p->scond;
				p->scond = C_SCOND_NONE;

				// g->panicwrap -= autosize;
				// MOVW panicwrap_offset(g), R3
				// SUB $autosize, R3
				// MOVW R3 panicwrap_offset(g)
				p->as = AMOVW;
				p->from.type = D_OREG;
				p->from.reg = REGG;
				p->from.offset = 2*ctxt->arch->ptrsize;
				p->to.type = D_REG;
				p->to.reg = 3;
				p = appendp(ctxt, p);
			
				p->as = ASUB;
				p->from.type = D_CONST;
				p->from.offset = autosize;
				p->to.type = D_REG;
				p->to.reg = 3;
				p = appendp(ctxt, p);

				p->as = AMOVW;
				p->from.type = D_REG;
				p->from.reg = 3;
				p->to.type = D_OREG;
				p->to.reg = REGG;
				p->to.offset = 2*ctxt->arch->ptrsize;
				p = appendp(ctxt, p);

				p->scond = scond;
			}

			p->as = AMOVW;
			p->scond |= C_PBIT;
			p->from.type = D_OREG;
			p->from.offset = autosize;
			p->from.reg = REGSP;
			p->to.type = D_REG;
			p->to.reg = REGPC;
			// If there are instructions following
			// this ARET, they come from a branch
			// with the same stackframe, so no spadj.
			
			if(p->to.sym) { // retjmp
				p->to.reg = REGLINK;
				q2 = appendp(ctxt, p);
				q2->as = AB;
				q2->to.type = D_BRANCH;
				q2->to.sym = p->to.sym;
				q2->pcond = p->to.sym->text;
				p->to.sym = nil;
				p = q2;
			}
			break;

		case AADD:
			if(p->from.type == D_CONST && p->from.reg == NREG && p->to.type == D_REG && p->to.reg == REGSP)
				p->spadj = -p->from.offset;
			break;

		case ASUB:
			if(p->from.type == D_CONST && p->from.reg == NREG && p->to.type == D_REG && p->to.reg == REGSP)
				p->spadj = p->from.offset;
			break;

		case ADIV:
		case ADIVU:
		case AMOD:
		case AMODU:
			if(ctxt->debugdivmod)
				break;
			if(p->from.type != D_REG)
				break;
			if(p->to.type != D_REG)
				break;
			q1 = p;

			/* MOV a,4(SP) */
			p = appendp(ctxt, p);
			p->as = AMOVW;
			p->lineno = q1->lineno;
			p->from.type = D_REG;
			p->from.reg = q1->from.reg;
			p->to.type = D_OREG;
			p->to.reg = REGSP;
			p->to.offset = 4;

			/* MOV b,REGTMP */
			p = appendp(ctxt, p);
			p->as = AMOVW;
			p->lineno = q1->lineno;
			p->from.type = D_REG;
			p->from.reg = q1->reg;
			if(q1->reg == NREG)
				p->from.reg = q1->to.reg;
			p->to.type = D_REG;
			p->to.reg = REGTMP;
			p->to.offset = 0;

			/* CALL appropriate */
			p = appendp(ctxt, p);
			p->as = ABL;
			p->lineno = q1->lineno;
			p->to.type = D_BRANCH;
			p->pcond = p;
			switch(o) {
			case ADIV:
				p->to.sym = ctxt->sym_div;
				break;
			case ADIVU:
				p->to.sym = ctxt->sym_divu;
				break;
			case AMOD:
				p->to.sym = ctxt->sym_mod;
				break;
			case AMODU:
				p->to.sym = ctxt->sym_modu;
				break;
			}

			/* MOV REGTMP, b */
			p = appendp(ctxt, p);
			p->as = AMOVW;
			p->lineno = q1->lineno;
			p->from.type = D_REG;
			p->from.reg = REGTMP;
			p->from.offset = 0;
			p->to.type = D_REG;
			p->to.reg = q1->to.reg;

			/* ADD $8,SP */
			p = appendp(ctxt, p);
			p->as = AADD;
			p->lineno = q1->lineno;
			p->from.type = D_CONST;
			p->from.reg = NREG;
			p->from.offset = 8;
			p->reg = NREG;
			p->to.type = D_REG;
			p->to.reg = REGSP;
			p->spadj = -8;

			/* Keep saved LR at 0(SP) after SP change. */
			/* MOVW 0(SP), REGTMP; MOVW REGTMP, -8!(SP) */
			/* TODO: Remove SP adjustments; see issue 6699. */
			q1->as = AMOVW;
			q1->from.type = D_OREG;
			q1->from.reg = REGSP;
			q1->from.offset = 0;
			q1->reg = NREG;
			q1->to.type = D_REG;
			q1->to.reg = REGTMP;

			/* SUB $8,SP */
			q1 = appendp(ctxt, q1);
			q1->as = AMOVW;
			q1->from.type = D_REG;
			q1->from.reg = REGTMP;
			q1->reg = NREG;
			q1->to.type = D_OREG;
			q1->to.reg = REGSP;
			q1->to.offset = -8;
			q1->scond |= C_WBIT;
			q1->spadj = 8;

			break;
		case AMOVW:
			if((p->scond & C_WBIT) && p->to.type == D_OREG && p->to.reg == REGSP)
				p->spadj = -p->to.offset;
			if((p->scond & C_PBIT) && p->from.type == D_OREG && p->from.reg == REGSP && p->to.reg != REGPC)
				p->spadj = -p->from.offset;
			if(p->from.type == D_CONST && p->from.reg == REGSP && p->to.type == D_REG && p->to.reg == REGSP)
				p->spadj = -p->from.offset;
			break;
		}
	}
}

static void
softfloat(Link *ctxt, LSym *cursym)
{
	Prog *p, *next, *psfloat;
	LSym *symsfloat;
	int wasfloat;

	if(!ctxt->debugfloat)
		return;

	symsfloat = linklookup(ctxt, "_sfloat", 0);
	psfloat = nil;
	if(symsfloat->type == STEXT)
		psfloat = symsfloat->text;

	wasfloat = 0;
	for(p = cursym->text; p != nil; p = p->link)
		if(p->pcond != nil)
			p->pcond->mark |= LABEL;
	for(p = cursym->text; p != nil; p = p->link) {
		switch(p->as) {
		case AMOVW:
			if(p->to.type == D_FREG || p->from.type == D_FREG)
				goto soft;
			goto notsoft;

		case AMOVWD:
		case AMOVWF:
		case AMOVDW:
		case AMOVFW:
		case AMOVFD:
		case AMOVDF:
		case AMOVF:
		case AMOVD:

		case ACMPF:
		case ACMPD:
		case AADDF:
		case AADDD:
		case ASUBF:
		case ASUBD:
		case AMULF:
		case AMULD:
		case ADIVF:
		case ADIVD:
		case ASQRTF:
		case ASQRTD:
		case AABSF:
		case AABSD:
			goto soft;

		default:
			goto notsoft;

		soft:
			if (psfloat == nil)
				ctxt->diag("floats used with _sfloat not defined");
			if (!wasfloat || (p->mark&LABEL)) {
				next = ctxt->arch->prg();
				*next = *p;

				// BL _sfloat(SB)
				*p = zprg;
				p->link = next;
				p->as = ABL;
 				p->to.type = D_BRANCH;
				p->to.sym = symsfloat;
				p->pcond = psfloat;
				p->lineno = next->lineno;

				p = next;
				wasfloat = 1;
			}
			break;

		notsoft:
			wasfloat = 0;
		}
	}
}

static Prog*
stacksplit(Link *ctxt, Prog *p, int32 framesize)
{
	int32 arg;

	// MOVW			g_stackguard(g), R1
	p = appendp(ctxt, p);
	p->as = AMOVW;
	p->from.type = D_OREG;
	p->from.reg = REGG;
	p->to.type = D_REG;
	p->to.reg = 1;
	
	if(framesize <= StackSmall) {
		// small stack: SP < stackguard
		//	CMP	stackguard, SP
		p = appendp(ctxt, p);
		p->as = ACMP;
		p->from.type = D_REG;
		p->from.reg = 1;
		p->reg = REGSP;
	} else if(framesize <= StackBig) {
		// large stack: SP-framesize < stackguard-StackSmall
		//	MOVW $-framesize(SP), R2
		//	CMP stackguard, R2
		p = appendp(ctxt, p);
		p->as = AMOVW;
		p->from.type = D_CONST;
		p->from.reg = REGSP;
		p->from.offset = -framesize;
		p->to.type = D_REG;
		p->to.reg = 2;
		
		p = appendp(ctxt, p);
		p->as = ACMP;
		p->from.type = D_REG;
		p->from.reg = 1;
		p->reg = 2;
	} else {
		// Such a large stack we need to protect against wraparound
		// if SP is close to zero.
		//	SP-stackguard+StackGuard < framesize + (StackGuard-StackSmall)
		// The +StackGuard on both sides is required to keep the left side positive:
		// SP is allowed to be slightly below stackguard. See stack.h.
		//	CMP $StackPreempt, R1
		//	MOVW.NE $StackGuard(SP), R2
		//	SUB.NE R1, R2
		//	MOVW.NE $(framesize+(StackGuard-StackSmall)), R3
		//	CMP.NE R3, R2
		p = appendp(ctxt, p);
		p->as = ACMP;
		p->from.type = D_CONST;
		p->from.offset = (uint32)StackPreempt;
		p->reg = 1;

		p = appendp(ctxt, p);
		p->as = AMOVW;
		p->from.type = D_CONST;
		p->from.reg = REGSP;
		p->from.offset = StackGuard;
		p->to.type = D_REG;
		p->to.reg = 2;
		p->scond = C_SCOND_NE;
		
		p = appendp(ctxt, p);
		p->as = ASUB;
		p->from.type = D_REG;
		p->from.reg = 1;
		p->to.type = D_REG;
		p->to.reg = 2;
		p->scond = C_SCOND_NE;
		
		p = appendp(ctxt, p);
		p->as = AMOVW;
		p->from.type = D_CONST;
		p->from.offset = framesize + (StackGuard - StackSmall);
		p->to.type = D_REG;
		p->to.reg = 3;
		p->scond = C_SCOND_NE;
		
		p = appendp(ctxt, p);
		p->as = ACMP;
		p->from.type = D_REG;
		p->from.reg = 3;
		p->reg = 2;
		p->scond = C_SCOND_NE;
	}
	
	// MOVW.LS		$framesize, R1
	p = appendp(ctxt, p);
	p->as = AMOVW;
	p->scond = C_SCOND_LS;
	p->from.type = D_CONST;
	p->from.offset = framesize;
	p->to.type = D_REG;
	p->to.reg = 1;

	// MOVW.LS		$args, R2
	p = appendp(ctxt, p);
	p->as = AMOVW;
	p->scond = C_SCOND_LS;
	p->from.type = D_CONST;
	arg = ctxt->cursym->text->to.offset2;
	if(arg == 1) // special marker for known 0
		arg = 0;
	if(arg&3)
		ctxt->diag("misaligned argument size in stack split");
	p->from.offset = arg;
	p->to.type = D_REG;
	p->to.reg = 2;

	// MOVW.LS	R14, R3
	p = appendp(ctxt, p);
	p->as = AMOVW;
	p->scond = C_SCOND_LS;
	p->from.type = D_REG;
	p->from.reg = REGLINK;
	p->to.type = D_REG;
	p->to.reg = 3;

	// BL.LS		runtime.morestack(SB) // modifies LR, returns with LO still asserted
	p = appendp(ctxt, p);
	p->as = ABL;
	p->scond = C_SCOND_LS;
	p->to.type = D_BRANCH;
	p->to.sym = ctxt->symmorestack[0];
	
	// BLS	start
	p = appendp(ctxt, p);
	p->as = ABLS;
	p->to.type = D_BRANCH;
	p->pcond = ctxt->cursym->text->link;
	
	return p;
}

static void
initdiv(Link *ctxt)
{
	if(ctxt->sym_div != nil)
		return;
	ctxt->sym_div = linklookup(ctxt, "_div", 0);
	ctxt->sym_divu = linklookup(ctxt, "_divu", 0);
	ctxt->sym_mod = linklookup(ctxt, "_mod", 0);
	ctxt->sym_modu = linklookup(ctxt, "_modu", 0);
}

static void xfol(Link*, Prog*, Prog**);

static void
follow(Link *ctxt, LSym *s)
{
	Prog *firstp, *lastp;

	ctxt->cursym = s;

	firstp = ctxt->arch->prg();
	lastp = firstp;
	xfol(ctxt, s->text, &lastp);
	lastp->link = nil;
	s->text = firstp->link;
}

static int
relinv(int a)
{
	switch(a) {
	case ABEQ:	return ABNE;
	case ABNE:	return ABEQ;
	case ABCS:	return ABCC;
	case ABHS:	return ABLO;
	case ABCC:	return ABCS;
	case ABLO:	return ABHS;
	case ABMI:	return ABPL;
	case ABPL:	return ABMI;
	case ABVS:	return ABVC;
	case ABVC:	return ABVS;
	case ABHI:	return ABLS;
	case ABLS:	return ABHI;
	case ABGE:	return ABLT;
	case ABLT:	return ABGE;
	case ABGT:	return ABLE;
	case ABLE:	return ABGT;
	}
	sysfatal("unknown relation: %s", anames5[a]);
	return 0;
}

static void
xfol(Link *ctxt, Prog *p, Prog **last)
{
	Prog *q, *r;
	int a, i;

loop:
	if(p == nil)
		return;
	a = p->as;
	if(a == AB) {
		q = p->pcond;
		if(q != nil && q->as != ATEXT) {
			p->mark |= FOLL;
			p = q;
			if(!(p->mark & FOLL))
				goto loop;
		}
	}
	if(p->mark & FOLL) {
		for(i=0,q=p; i<4; i++,q=q->link) {
			if(q == *last || q == nil)
				break;
			a = q->as;
			if(a == ANOP) {
				i--;
				continue;
			}
			if(a == AB || (a == ARET && q->scond == 14) || a == ARFE || a == AUNDEF)
				goto copy;
			if(q->pcond == nil || (q->pcond->mark&FOLL))
				continue;
			if(a != ABEQ && a != ABNE)
				continue;
		copy:
			for(;;) {
				r = ctxt->arch->prg();
				*r = *p;
				if(!(r->mark&FOLL))
					print("can't happen 1\n");
				r->mark |= FOLL;
				if(p != q) {
					p = p->link;
					(*last)->link = r;
					*last = r;
					continue;
				}
				(*last)->link = r;
				*last = r;
				if(a == AB || (a == ARET && q->scond == 14) || a == ARFE || a == AUNDEF)
					return;
				r->as = ABNE;
				if(a == ABNE)
					r->as = ABEQ;
				r->pcond = p->link;
				r->link = p->pcond;
				if(!(r->link->mark&FOLL))
					xfol(ctxt, r->link, last);
				if(!(r->pcond->mark&FOLL))
					print("can't happen 2\n");
				return;
			}
		}
		a = AB;
		q = ctxt->arch->prg();
		q->as = a;
		q->lineno = p->lineno;
		q->to.type = D_BRANCH;
		q->to.offset = p->pc;
		q->pcond = p;
		p = q;
	}
	p->mark |= FOLL;
	(*last)->link = p;
	*last = p;
	if(a == AB || (a == ARET && p->scond == 14) || a == ARFE || a == AUNDEF){
		return;
	}
	if(p->pcond != nil)
	if(a != ABL && a != ABX && p->link != nil) {
		q = brchain(ctxt, p->link);
		if(a != ATEXT && a != ABCASE)
		if(q != nil && (q->mark&FOLL)) {
			p->as = relinv(a);
			p->link = p->pcond;
			p->pcond = q;
		}
		xfol(ctxt, p->link, last);
		q = brchain(ctxt, p->pcond);
		if(q == nil)
			q = p->pcond;
		if(q->mark&FOLL) {
			p->pcond = q;
			return;
		}
		p = q;
		goto loop;
	}
	p = p->link;
	goto loop;
}

LinkArch linkarm = {
	.name = "arm",

	.addstacksplit = addstacksplit,
	.assemble = span5,
	.datasize = datasize,
	.follow = follow,
	.iscall = iscall,
	.isdata = isdata,
	.ldobj = ldobj5,
	.nopout = nopout5,
	.prg = prg,
	.settextflag = settextflag,
	.symtype = symtype,
	.textflag = textflag,
	.zfile = zfile,
	.zhist = zhist,
	.zname = zname,
	.zprog = zprog,

	.minlc = 4,
	.ptrsize = 4,

	.D_ADDR = D_ADDR,
	.D_BRANCH = D_BRANCH,
	.D_CONST = D_CONST,
	.D_EXTERN = D_EXTERN,
	.D_FCONST = D_FCONST,
	.D_NONE = D_NONE,
	.D_PCREL = D_PCREL,
	.D_SCONST = D_SCONST,
	.D_SIZE = D_SIZE,

	.ACALL = ABL,
	.AFUNCDATA = AFUNCDATA,
	.AJMP = AB,
	.ANOP = ANOP,
	.APCDATA = APCDATA,
	.ARET = ARET,
	.ATEXT = ATEXT,
	.AUSEFIELD = AUSEFIELD,
};