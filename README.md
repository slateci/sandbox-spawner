For an overview of the overall sandbox setup, [see here](https://github.com/slateci/sandbox-portal).

# SLATE Sandbox Spawner

The sandbox spawner is a web service that runs locally on sandbox.slateci.io and manages the user containers within the kubernetes cluster. It uses [Crow](https://crowcpp.org/) as its web framework. The main source code file is [sandbox_spawner.cpp](https://github.com/slateci/sandbox-spawner/blob/master/src/sandbox_spawner.cpp) which includes:
* registering and setting up user account and deployment - the logic is encoded in the [createAccount](https://github.com/slateci/sandbox-spawner/blob/master/src/sandbox_spawner.cpp#L290) function, which takes in the user information, creates authentication token for ttyd, assigns the port and deploys the container
* the actual Kubernetes deployment descriptor - this is hardcoded in the source file as the [deploymentTemplate](https://github.com/slateci/sandbox-spawner/blob/master/src/sandbox_spawner.cpp#L205) static variable
* the management of the user data - this is done through the [DataStore](https://github.com/slateci/sandbox-spawner/blob/master/src/sandbox_spawner.cpp#L129) data structure, which is also responsible to serialize/deserialize the data
* the assignment of the ports - the range is hardcoded in the [getPort](https://github.com/slateci/sandbox-spawner/blob/master/src/sandbox_spawner.cpp#L160) function

# Current monitoring

Check_mk is setup to monitor whether the spawner process is alive, and report if it is not.
