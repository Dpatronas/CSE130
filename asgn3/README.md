README httpproxy

Overview:
	This program is a multi-threaded HTTP proxy server, acting as both client and server. 
	The proxy maintains persist connection to Clients

	The proxy accepts GET commands from Clients.
		- If the request line is found to be invalid
			- Proxy will send a 400 "Bad Request" response to Client 

	If proxy receives any other command from Client.
		- Proxy will send a 501 "Not Implemented" response to Client

	Port-Forwarding:
		The proxy port forwards valid GET requests to HTML servers (specified on startup through CLI).
			- Proxy has server send response to client directly
				Note: servers are not dedicated to the proxy 
					ie: no assumptions made about server external operations

					Only port forwards to servers who are responsive.

	Load-Balancing:
		The proxy load balances by choosing servers to port forward requests to.
			- Probes servers to send their logs to proxy every -R requests.
				- Skips servers who are offline or have sent corrupted logs.
				- Prioritizes online servers based on:
					1) Least entries in its log
					2) If there is a tie between multiple servers for (1), 
						proxy chooses a server with least errors in its log.

					3) If there is a tie for multiple servers in (1) and (2),
						proxy chooses the first server in line of CLI.

			- If no servers are online or able to respond to proxy with clean log reports,
			Proxy sends Client a 500 "Internal Server Error" response to Client
		

Makefile:
	make httpproxy	 : Builds executable for program
	make clean  	 : Removes all generated files

Usage:
	Arguments can be processed in any order received in the command line.

	Mandatory CLI Arguments:
		Must include a valid proxy port number argument (processed as first non-option argument)
		Must include at least one server port number	(subsequest non-option arguments)
		
	Optional CLI Arguments: 	Use						Default values:
		-N threads 		set threads					5
		-r frequency		how often to probe server for portforwarding	5 (every 5 requests)
		
	Usage Examples:
	Ex 1:	./httpproxy 1234 8080		proxy listening for Clients on port 1234
						proxy connects to a server on port 8080
						threads = 5, frequency = 5

	Ex 2:	./httpproxy -N 32 1234 8081	proxy listening for Clients on port 1234
						proxy connects to a server on port 8081
						threads = 32, frequency = 5

	Ex 3:	./httpproxy 1234 8081 8082 8083	proxy listening for Clients on port 1234
						proxy connects to servers on ports: 8081, 8082, 8083
						threads = 5, frequency = 5

	Ex 4:	./httpproxy 1234 8084 -r 2 8085	proxy listening for Clients on port 1234
						proxy connects to servers on ports: 8084, 8085
						threads = 5, frequency = 2
							
	Using Proxy as a Client (Note: proxy and server(s) should be running to receive responses from proxy):
		
		curl -s http://localhost:8080/file2	(-s = GET command)
		// Prints contents of file2 back to Client if it is available in proxy server directory

		curl -s http://localhost:8080/-file2	(-s = GET command)
		// Returns a 400 response to Client






