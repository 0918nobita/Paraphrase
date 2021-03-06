"usage" :
	<<<
		----------------------------------
		usage: ../para PE21mt.pp 10000 run
       \           or
       \       ../para PE21mt.pp test
		----------------------------------
	>>> write
;

( { depth 0 == } { interactive? not } ) and if
	args size 0? if usage then
then

// n --- d-func-value
// For more information about d-func-value, see https://projecteuler.net/problem=21 .
"d-func" :
	0 ( `n `result ) local
	1 `n , 2 / 1+ for+
		`n , i % 0 == if `result , i + `result let then
	next
	`result ,
;

// N --- array
"make-d-array/mt" :
	`n local

	reset-pipes
	`n , 1 + new-array

	`n , 
	[-> `n local 2 `n , 1 + for+ i >pipe next ]
	[[ while-pipe dup d-func tuple >pipe repeat ]]

	while-pipe /* array (t d-value-of-t) */
		de-list	@set
	repeat
	0 0 @set	// d(0):=0
	1 1 @set	// d(1):=1 
;

// array from to --- array
"getAmicableNumbersFromArray/mt" :
	reset-pipes
	tuple	// array ( from to ) 

	/* init DS by the TOS (list:( from to )). */
	[-> de-list /* from to */ 1+ for+ i >pipe next ]

	/* --- worker --- */
	/* init DS by the TOS (array). */
	[[->	// TOS is 'd-value' array.
 		while-pipe
			`t local
			`t , get `t , == if continue then	// skip if t is perfect number.
			`t , get valid-index? if 
				get `t , == if `t , >pipe then
			else
				drop
			then
		repeat
	]]

	/* --- gather (or reduce) --- */
	[ () while-pipe append repeat >pipe ] 
	pipe>
;

// n --- n(=total)
"printAmicableNumbers/mt" :
	`n local

	`n , make-d-array/mt 2 `n ,	// array 2 n 
	getAmicableNumbersFromArray/mt { < } sort
	dup . cr
	0 `total local
	while @not-empty-list? do
		pop-front `total , + `total let
	repeat
	drop `total , 
;

"run" : 
	printAmicableNumbers/mt .cr
;

"test" :
	use-mock-stdout
		20000 printAmicableNumbers/mt
	use-stdout
	115818 != if "NG" .cr -1 exit then
	"GOOD" .cr
;


