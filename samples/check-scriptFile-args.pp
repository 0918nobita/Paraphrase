// usage: para -E "1 2.3 'str' ( a ( b c ) )" check-scriptFile-args.pp
depth 0? if
	"usage: para -E \"1 2.3 'str' ( a ( b c ) )\" check-scriptFile-args.pp" .cr
	"  or : para -E \"1 2.3 'str' ( a ( b c ) )\" check-scriptFile-args.pp test" .cr
else
	args size 0? if show then
then

"check" : == not if "NG" .cr -1 exit then ;
"test" :
	( a ( b c ) ) check 
	"str" check
	2.3 check
	1 check
	"GOOD" .cr
;

