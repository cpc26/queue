%{
/*
$Revision

 */
#include "lex.h"

struct klist {
	char *keyword;
	enum keyword keyvalue;
} klist[] = {
	"exec",		K_EXEC,
	"maxexec",	K_MAXEXEC,
	"vmaxexec",	K_VMAXEXEC,
        "pfactor",      K_PFACTOR,
	"maxqueue",	K_MAXQUEUE,
	"supervisor",	K_SUPERVISOR,
	"mail",		K_MAIL,
        "host",         K_HOST,
	"mailsupervisor", K_MAILSUPERVISOR,
	"console",	K_CONSOLE,
	"nice",		K_NICE,
	"restart",	K_RESTART,
	"timestop",	K_TIMESTOP,
	"timesched",	K_TIMESCHED,
	"day",		K_DAY,
	"off",		K_OFF,
	"drain",	K_DRAIN,
	"on",		K_ON,
	"loadstop",	K_LOADSTOP,
	"loadsched",	K_LOADSCHED,
        "loadcheckpoint", K_LOADCHECKPOINT,
        "checkpointmode", K_CHECKPOINTMODE,
        "restartmode", K_RESTARTMODE,
        "restartflag", K_RESTARTFLAG,
	"minfree",	K_MINFREE,
	"program",	K_PROGRAM,
	"cpulimit",	K_RLIMITCPU, /* historic synonym for rlimitcpu */
	"rlimitcpu",	K_RLIMITCPU,
	"rlimitfsize",	K_RLIMITFSIZE,
	"rlimitdata",	K_RLIMITDATA,
	"rlimitstack",	K_RLIMITSTACK,
	"rlimitcore",	K_RLIMITCORE,
	"rlimitrss",	K_RLIMITRSS,
        "rlimitnofile", K_RLIMITNOFILE,
        "rlimitvmem", K_RLIMITVMEM,
        "rlimitas", K_RLIMITVMEM, /*synonym for vmem*/
	0
};

%}

%option yylineno

%%

-?[0-9]+			return (int)K_NUMBER;

[/0-9a-zA-Z_.,-]+	{
				register struct klist *kp;
				for(kp = klist ; kp->keyword ; kp++)
					if(strcmp(kp->keyword, yytext) == 0)
						return (int)kp->keyvalue;
				return (int)K_VARIABLE;
			}

\n				return (int)K_LINE;

[ \t]				;

^#.*$				;	/* comment */

.				return (int)K_SYNTAX;

%%

lexfile(f)
FILE *f;
{
        yyrestart(f);
#if 0
	extern struct yysvf *yylstate [YYLMAX], **yylsp, **yyolsp;
	extern int *yyfnd, yyprevious;

	yylineno = 1;
	bzero(yytext, YYLMAX*sizeof yytext[0]);
	bzero(yylstate, sizeof yylstate);
	yylsp = yyolsp = 0;
	bzero(yysbuf, YYLMAX*sizeof yysbuf[0]);
	yysptr = yysbuf;
	yyfnd = 0;
	yyprevious = YYNEWLINE;
	yyleng = 0;
	yymorfg = 0;
	yytchar = 0;
	yyin = f;
	yyestate = 0;
#endif
}

yywrap()
{
	return 1;
}
