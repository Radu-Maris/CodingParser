%{
#define NUMBER 256
#define IF 258
#define ELSE 259
#define WHILE 260
int yylval;
%}

%%

[0-9]+ { yylval = atoi(yytext); return NUMBER; }
[{}+()=\n;] return *yytext;
if return IF;
else return ELSE;
while return WHILE;
