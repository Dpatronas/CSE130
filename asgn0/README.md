README

	This program is a limited version head cmd in Ubuntu's Unix version 18.04.
	This program has the following variations:

		- shoulders must take an n argument (where n is number of lines)
		- program is written in quiet mode
		- does not support any flags
		- CLI file argument w/ '-' (DASH) in front will trigger STDIN

		example of head v shoulders usage:
				HEAD			SHOULDERS
			head -qn 10 file1 file2 == shoulders 10 file1 file2
			head -qn 10		== shoulders 10

	shoulders.c
		Implementation to mimic unix head cmd.

	Makefile
		make shoulders	 : Builds executable for program
		make clean  	 : Removes all generated files

	usage example:
		$./shoulders n  file1 file2		(where n is a non negative number)
