#
/*
 * pi - Pascal interpreter code translator
 *
 * Charles Haley, Bill Joy UCB
 * Version 1.0 August 1977
 */

#include "whoami"
#include "0.h"
#include "tree.h"

/*
 * Const enters the definitions
 * of the constant declaration
 * part into the namelist.
 */
constbeg()
{

	if (parts & (TPRT|VPRT))
		error("Constant declarations must precede type and variable declarations");
	if (parts & CPRT)
		error("All constants must be declared in one const part");
	parts =| CPRT;
}

const(cline, cid, cdecl)
	int cline;
	char *cid;
	int *cdecl;
{
	register struct nl *np;

	line = cline;
	gconst(cdecl);
	np = enter(defnl(cid, CONST, con.ctype, con.cival));
	np->nl_flags =| NMOD;
	if (con.ctype == NIL)
		return;
	if (isa(con.ctype, "i"))
		np->range[0] = con.crval;
	else if (isa(con.ctype, "d"))
		np->real = con.crval;
}

constend()
{

}

/*
 * Gconst extracts
 * a constant declaration
 * from the tree for it.
 * only types of constants
 * are integer, reals, strings
 * and scalars, the first two
 * being possibly signed.
 */
gconst(r)
	int *r;
{
	register struct nl *np;
	register *cn;
	char *cp;
	int negd, sgnd;
	long ci;

	con.ctype = NIL;
	cn = r;
	negd = sgnd = 0;
loop:
	if (cn == NIL || cn[1] == NIL)
		return (NIL);
	switch (cn[0]) {
		default:
			panic("gconst");
		case T_MINUSC:
			negd = 1 - negd;
		case T_PLUSC:
			sgnd++;
			cn = cn[1];
			goto loop;
		case T_ID:
			np = lookup(cn[1]);
			if (np == NIL)
				return;
			if (np->class != CONST) {
				error("%s is a %s, not a constant as required", cn[1], classes[np->class]);
				return;
			}
			con.ctype = np->type;
			switch (classify(np->type)) {
				case TINT:
					con.crval = np->range[0];
					break;
				case TDOUBLE:
					con.crval = np->real;
					break;
				case TBOOL:
				case TCHAR:
				case TSTR:
				case TSCAL:
					con.cival = np->value[0];
					con.crval = con.cival;
					break;
				case NIL:
					con.ctype = NIL;
					return;
				default:
					panic("gconst2");
			}
			break;
		case T_CBINT:
			con.crval = a8tol(cn[1]);
			goto restcon;
		case T_CINT:
			con.crval = atof(cn[1]);
			if (con.crval > MAXINT || con.crval < MININT) {
				error("Constant too large for this implementation");
				con.crval = 0;
			}
restcon:
			ci = con.crval;
			if (bytes(ci, ci) <= 2)
				con.ctype = nl+T2INT;
			else	
				con.ctype = nl+T4INT;
			break;
		case T_CFINT:
			con.ctype = nl+TDOUBLE;
			con.crval = atof(cn[1]);
			break;
		case T_CSTRNG:
			cp = cn[1];
			if (cp[1] == 0) {
				con.ctype = nl+T1CHAR;
				con.cival = cp[0];
				con.crval = con.cival;
				break;
			}
			con.ctype = nl+TSTR;
			con.cival = savestr(cp);
			con.crval = con.cival;
			break;
	}
	if (sgnd) {
		if (isnta(con.ctype, "id"))
			error("%s constants cannot be signed", nameof(con.ctype));
		else {
			if (negd)
				con.crval = -con.crval;
			ci = con.crval;
			if (bytes(ci, ci) <= 2)
				con.ctype = nl+T2INT;
		}
	}
}

isconst(r)
	register int *r;
{

	if (r == NIL)
		return (1);
	switch (r[0]) {
		case T_MINUS:
			r[0] = T_MINUSC;
			r[1] = r[2];
			return (isconst(r[1]));
		case T_PLUS:
			r[0] = T_PLUSC;
			r[1] = r[2];
			return (isconst(r[1]));
		case T_VAR:
			if (r[3] != NIL)
				return (0);
			r[0] = T_ID;
			r[1] = r[2];
			return (1);
		case T_BINT:
			r[0] = T_CBINT;
			r[1] = r[2];
			return (1);
		case T_INT:
			r[0] = T_CINT;
			r[1] = r[2];
			return (1);
		case T_FINT:
			r[0] = T_CFINT;
			r[1] = r[2];
			return (1);
		case T_STRNG:
			r[0] = T_CSTRNG;
			r[1] = r[2];
			return (1);
	}
	return (0);
}
