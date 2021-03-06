README httpserver

	Overview:
		This program is a multi-threaded HTML server. 
		The server responds to PUT, GET, HEAD, and commands. 
		The server maintains persist connection, and store files in directory which server is run. 
		The server has minimal error handling capabilities, sending a couple status codes.

	httpserver.c:
		- Parses curl request from Client to perform GET, HEAD, and PUT commands:
			- PUT : Receives file contents from Client. Stores contents into Client 
				requested file on server.
			- GET : Sends Client contents of requested file stored in server.
			- HEAD: Sends Client metadata of requested file stored in server.


		- Server only supports the following status codes for responses to Client:
			CODE  MESSAGE			USAGE
			- 200 OK			| Used when no other response is appropriate
			- 201 Created			| Sucessful PUT, which stores into new file
			- 400 Bad Request 		| Bad request line, ie. not able to parse
			- 403 Forbidden 		| GET or HEAD cannot access file due to permissions
			- 404 File Not Found		| GET or HEAD cannot find file requested
			- 500 Internal Server Error 	| Server cannot process a valid request
			- 501 Not Implemented		| Request is valid but command !(GET || HEAD || PUT)

	Makefile:
		make httpserver	 : Builds executable for program
		make clean  	 : Removes all generated files

	Usage:
		Mandatory Arguments:
			Must include a port number argument
				-any valid port # will be accepted
		
		Optional Arguments: -N threads -l logfile
			- Threads are defaulted to 5 unless otherwise -N argnum is passed through CLI
			- Arguments can be processed in any order received in the command line
		
		EX:		./httpserver 8080		(listening port 8080, threads default 5)
		EX:		./httpserver -N 32 8081 	(listening port 8081, threads set to 32)
		EX:		./httpserver -l log.txt -N 16	(log set to log.txt,  threads set to 16)
		EX:		./httpserver -l log 8082 -N 2	(log set to log,  port 8082, threads set to 2)
								
		Client End:
			PUT: puts file1 into file2 on server. file2 is either created or truncated
				curl -T file1 http://localhost:8080/file2	(-T = PUT command)

			GET: Prints contents of file2 back to Client.
				curl -s http://localhost:8080/file2		(-s = GET command)

			HEAD: Prints metadata of file2 back to Client
				curl -I http://localhost:8080/file2		(-I = HEAD command)





