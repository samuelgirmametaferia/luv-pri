grammar Luv;

program: (moduleDecl)? topLevel* EOF;

topLevel
    : useStmt
    | visibilityDecl
    | externDecl
    | statement
    ;

// Module declaration
moduleDecl: 'module' modulePath;

// Use/import statements
useStmt
    : 'use' useTarget 'from' modulePath    # useFromStmt
    | 'use' modulePath                      # usePathStmt
    ;

useTarget
    : '*'                                   # useAllPublic
    | AT                                    # useAllPrivate
    | IDENTIFIER                            # useSingle
    | '{' useList '}'                       # useSet
    ;

useList: IDENTIFIER (',' IDENTIFIER)* ','?;

modulePath: IDENTIFIER (PATH_SEP IDENTIFIER)*;

// Visibility wrapper
visibilityDecl: ('pub' | 'priv') (funcDecl | varDecl | externDecl | structDecl | classDecl | interfaceDecl | enumDecl);

externDecl: 'extern' (STRING)?  'fn'?  IDENTIFIER '(' params? ')' (':' type)? ';'?;

statement
    : funcDecl
    | structDecl
    | enumDecl
    | classDecl
    | interfaceDecl
    | ifExpr
    | whileExpr
    | forExpr
    | loopExpr
    | exprStmt
    | varDecl
    | returnStmt
    | breakStmt
    | continueStmt
    | block
    | ';'
    ;

breakStmt: 'break' IDENTIFIER? ';'?;
continueStmt: 'continue' IDENTIFIER? ';'?;

block: '{' statement* '}';

structDecl: (attribute)*  'struct'? IDENTIFIER '{' structMember* '}' ;
structMember: (attribute | 'pub' | 'priv' | 'static')* (structField | declaration) ;
structField: IDENTIFIER ':' type (';' | ',')?;

enumDecl: (attribute)* 'enum' IDENTIFIER '{' enumVariant* '}' ;
enumVariant: IDENTIFIER ('(' typeList ')')? ','? ;
typeList: type (',' type)* ','? ;

classDecl: (attribute)* 'abstract'? 'class' IDENTIFIER (':' IDENTIFIER (',' IDENTIFIER)*)? '{' classMember* '}' ;
classMember: (attribute | 'pub' | 'priv' | 'override' | 'static' | 'abstract')* (funcDecl | classField | declaration) ;
classField: IDENTIFIER ':' type (';' | ',')?;

declaration: structDecl | enumDecl | classDecl | interfaceDecl | funcDecl | varDecl ;

interfaceDecl: (attribute)* 'interface' IDENTIFIER (':' IDENTIFIER (',' IDENTIFIER)*)? '{' interfaceMember* '}' ;
interfaceMember: (attribute)*  'fn'?  IDENTIFIER '(' params? ')' (':' type)? ';'?? ;

varDecl
    : modifier+ bindingPatternList (':' type)? ('=' expr)? ';'?
    | bindingPatternList ':' type ('=' expr)? ';'?
    ;
bindingPatternList: bindingPattern (',' bindingPattern)* ','? ;
bindingPattern
    : IDENTIFIER                                    # identifierPattern
    | '(' bindingPatternList ')'                    # tuplePattern
    | IDENTIFIER '{' structBindingList? '}'         # structPattern
    | IDENTIFIER '(' bindingPatternList? ')'        # variantPattern
    | '_'                                           # wildcardPattern
    | literal ((RANGE | RANGE_INC) literal)?        # literalPattern
    ;

structBindingList: structBinding (',' structBinding)* ','? ;
structBinding: IDENTIFIER (':' bindingPattern)?;

modifier: 'mut' | 'const' | 'dyn' | 'static' | 'abstract' | 'override' | 'pub' | 'priv' | 'unique' | 'lent' | 'rc_shared' | 'owned_cow' | 'immutable' | 'escaped' | memoryHint | attribute ;

attribute
    : AT IDENTIFIER ('(' IDENTIFIER ')')?
    | '![' attrList ']'
    ;

memoryHint: AT ('stack' | 'heap' | 'rc' | 'arc' | 'gc' | 'pool' | 'static' | 'rss') ;

attrList: attr (',' attr)* ','? ;
attr: IDENTIFIER ('(' IDENTIFIER ')')?;

overloadableOp: '+' | '-' | '*' | '/' | '%' | '==' | '!=' | '<' | '>' | '<=' | '>=';
funcName: IDENTIFIER | overloadableOp;

funcDecl
    : (attribute)* 'fn'? ('&'? boundStruct=IDENTIFIER)? funcName typeParams? '(' params? ')' (':' type)? (block | (('=>' | '->') expr) | ';')
    ;

typeParams: '[' IDENTIFIER (',' IDENTIFIER)* ';'? ']';

params: param (',' param)* ','? ;
param: modifier* IDENTIFIER (':' type)? ('=' expr)?;

type: typeCore ('?' | '??')* ;

typeCore
    : 'int' | 'uint' | 'float'
    | 'string' | 'char' | 'bool' | 'bit' | 'bytes' | 'ptr'
    | 'void' | 'dyn' | 'tnt'
    | IDENTIFIER   // for types like i32, u64, T, U, etc.
    | '[' type (';' expr)? ']'                     // Array type
    | '(' type (',' type)* ','? ')'                // Tuple type
    ;


ifExpr: (attribute)* 'if' expr block (efExpr)* ('else' block)?;
efExpr: 'ef' expr block;

whileExpr: (attribute)* (IDENTIFIER ':')? 'while' expr block ('continue' block)?;

loopExpr: (attribute)* (IDENTIFIER ':')? 'loop' block;

forExpr
    : (attribute)* (IDENTIFIER ':')? 'for' modifier* bindingPatternList 'in' start=expr RANGE end=expr block     # forRangeExpr
    | (attribute)* (IDENTIFIER ':')? 'for' modifier* bindingPatternList 'in' start=expr RANGE_INC end=expr block # forRangeIncExpr
    | (attribute)* (IDENTIFIER ':')? 'for' modifier* bindingPatternList 'in' expr block                          # forInExpr
    | (attribute)* (IDENTIFIER ':')? 'for' start=expr (RANGE | RANGE_INC) end=expr block                         # forRepeatExpr
    | (attribute)* (IDENTIFIER ':')? 'for' (varDecl | expr) ';' expr ';' expr block            # forCStyle
    ;

returnStmt: 'return' expr? ';'?;
exprStmt: expr ';'?;

structInstFields: IDENTIFIER ':' expr (',' IDENTIFIER ':' expr)* ','? ;

assignmentTarget: singleAssignmentTarget (',' singleAssignmentTarget)* ;
singleAssignmentTarget: primary (('.' | '->') (IDENTIFIER | INT | FLOAT) | '[' expr ']' | '(' args? ')')* ;

expr
    : 'super' '.' IDENTIFIER '(' args? ')'                       # superCallExpr
    | expr ('.' | '->') (IDENTIFIER | INT | FLOAT) '(' args? ')' # methodCallExpr
    | expr ('.' | '->') (IDENTIFIER | INT | FLOAT)               # propertyExpr
    | expr AT INT                                                # bitAccessExpr
    | expr '[' expr ']'                                          # indexExpr
    | expr '[' expr (RANGE | RANGE_INC) expr (':' expr)? ']'     # sliceExpr
    | expr op=('++' | '--')                                      # postfixExpr
    | expr op=('as' | '->' | 'as!' | '|>') type                  # castExpr
    | op=('!' | 'not' | '~' | '-') expr                          # unaryExpr
    | left=expr op=('*' | '/' | '%') right=expr                  # multiplicativeExpr
    | left=expr op=('+' | '-') right=expr                        # additiveExpr
    | left=expr op=('<<' | '>>') right=expr                      # shiftExpr
    | left=expr op=('<' | '>' | '<=' | '>=' | '==' | '!=' ) right=expr          # comparisonExpr
    | left=expr op='&' right=expr                                # bitwiseAndExpr
    | left=expr op='^' right=expr                                # bitwiseXorExpr
    | left=expr op='|' right=expr                                # bitwiseOrExpr
    | left=expr op=('&&' | 'and') right=expr                     # logicalAndExpr
    | left=expr op=('||' | 'or') right=expr                      # logicalOrExpr
    | left=expr op='??' right=expr                               # nullCoalescingExpr
    | cond=expr '?' (thenExpr=expr)? ':' elseExpr=expr           # ternaryExpr
    | IDENTIFIER '{' structInstFields? '}'                       # structInstExpr
    | IDENTIFIER '(' args? ')'                                   # callExpr
    | IDENTIFIER '[' type (',' type)* ';'? ']' '(' args? ')'     # genericCallExpr
    | AT IDENTIFIER '(' args? ')'                                # intrinsicCallExpr
    | 'asm' '{' (STRING | BACKTICK_STRING) ('(' args? ')')? ('rtn' expr)? '}' # asmExpr
    | 'match' expr '{' matchCase* '}'                            # matchExpr
    | ifExpr                                                     # ifExprAlternative
    | forExpr                                                    # forExprAlternative
    | whileExpr                                                  # whileExprAlternative
    | loopExpr                                                   # loopExprAlternative
    | primary                                                    # primaryExpr
    | target=assignmentTarget op=('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | '>>=') value=expr # assignmentExpr
    ;
matchCase: (pattern=bindingPattern) '=>' (resultExpr=expr | resultBlock=block) ','? ;

args: expr (',' expr)* ','? ;

literal
    : INT                                  # intLit
    | FLOAT                                # floatLit
    | STRING                               # stringLit
    | BACKTICK_STRING                      # stringLit
    | BYTES_LIT                            # bytesLit
    | CHAR                                 # charLit
    | BOOL                                 # boolLit
    | 'nen'                                # nullLit
    ;

primary
    : literal                              # primaryLiteral
    | '&' STRING                           # stringInterpolationExpr
    | '&' BACKTICK_STRING                  # stringInterpolationExpr
    | IDENTIFIER                           # identifier
    | '(' expr ')'                         # groupingExpr
    | '(' expr (',' expr)+ ','? ')'        # tupleExpr
    | '[' args? ']'                        # arrayExpr
    | '[' expr ';' expr ']'                # arrayRepeatExpr
    | '[' expr 'for' IDENTIFIER 'in' expr ( (RANGE | RANGE_INC) expr ('if' expr)? )? ']' # arrayCompExpr
    ;

// Lexer
AT: '@';
PATH_SEP: '::';
RANGE_INC: '...';
RANGE: '..';
FLOAT: [0-9]+ '.' [0-9]+;
INT: [0-9]+ | '0x' [0-9a-fA-F]+ | '0b' [01]+;
STRING: '"' (~["\\] | '\\' .)* '"';
BACKTICK_STRING: '`' (~[`\\] | '\\' .)* '`';
CHAR: '\'' (~['\\\r\n] | '\\' .) '\'';
BOOL: 'true' | 'false';
BYTES_LIT: 'b' ('"' (~["\\] | '\\' .)* '"' | '`' (~[`\\] | '\\' .)* '`');
MUT: 'mut';
CONST: 'const';
DYN: 'dyn';
FN: 'fn' ;
STRUCT: 'struct';
ENUM: 'enum';
CLASS: 'class';
INTERFACE: 'interface';
MATCH: 'match';
RTN: 'rtn';

IDENTIFIER: [a-zA-Z_] [a-zA-Z0-9_]*;

WS: [ \t\r\n]+ -> skip;
COMMENT: '#' ~[\r\n]* -> skip;
LINE_COMMENT: '//' ~[\r\n]* -> skip;
