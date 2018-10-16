#include <cerrno>
#include <cstdio> //for rename, remove
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <map>
#include <set>

#define CROW_ENABLE_SSL
#include <crow.h>

#include <boost/optional.hpp>

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/nvp.hpp>
#include <boost/serialization/map.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <HTTPRequests.h>
#include <Process.h>
#include <Utilities.h>
#include <base64.h>

struct Configuration{
	std::string portString;
	std::string sslCertificate;
	std::string sslKey;
	std::string slateEndpoint;
	std::string slateAdminToken;
	
	Configuration():
	portString("18081"),
	slateEndpoint("http://localhost:18080"),
	slateAdminToken("3acc9bdc-1243-40ea-96df-373c8a616a16")
	{}
} config;

struct UserData{
	std::string deploymentName;
	std::string podName;
	std::string serviceName;
	unsigned int servicePort;
	std::string secretName;
	std::string authToken;
	std::string slateID;
	std::string slateToken;
	
	template<typename Archive>
	void serialize(Archive& ar, const unsigned int file_version);
};

template<typename Archive>
void UserData::serialize(Archive& ar, const unsigned int file_version){
	using boost::serialization::make_nvp;
	ar & make_nvp("deployment",deploymentName);
	ar & make_nvp("pod",podName);
	ar & make_nvp("service",serviceName);
	ar & make_nvp("port",servicePort);
	ar & make_nvp("secret",secretName);
	ar & make_nvp("auth",authToken);
	ar & make_nvp("slateID",slateID);
	ar & make_nvp("slateToken",slateToken);
}

struct TokenGenerator{
public:
	std::string getToken(){
		std::lock_guard<std::mutex> lock(mut);
		boost::uuids::uuid id = gen();
		return to_string(id);
	}
private:
	std::mutex mut;
	boost::uuids::random_generator gen;
} tokenGenerator;

class DataStore{
public:
	DataStore(const std::string dataPath):persistentPath(dataPath){
		loadData();
	}
	
	boost::optional<UserData> find(const std::string& globusID){
		std::lock_guard<std::mutex> lock(mut);
		auto it=podMap.find(globusID);
		if(it==podMap.end())
			return {};
		return it->second;
	}
	
	void record(const std::string& globusID, const UserData& data){
		std::lock_guard<std::mutex> lock(mut);
		podMap[globusID]=data;
		usedPorts.insert(data.servicePort);
		saveData();
	}
	
	unsigned int getPort(){
		std::lock_guard<std::mutex> lock(mut);
		for(unsigned int port=5000; port<10000; port++){
			if(!usedPorts.count(port))
				return port;
		}
		throw std::runtime_error("port range exhausted");
	}
	
private:
	std::mutex mut;
	std::string persistentPath;
	std::map<std::string,UserData> podMap;
	std::set<unsigned int> usedPorts;
	
	//\pre mut held
	void saveData(){
		const static std::string tmpName="tmp_data";
		std::ofstream out(tmpName);
		boost::archive::text_oarchive ar(out);
		ar << podMap;
		out.close();
		rename(tmpName.c_str(),persistentPath.c_str());
	}
	//\pre mut held
	void loadData(){
		std::ifstream in(persistentPath);
		if(!in){
			std::cout << "Unable to read '" << persistentPath 
			  << "'; continuing with no saved user data" << std::endl;
			return;
		}
		boost::archive::text_iarchive ar(in);
		ar >> podMap;
		for(const auto& account : podMap)
			usedPorts.insert(account.second.servicePort);
	}
};

const static std::string namePattern="{{name}}";
const static std::string authPattern="{{auth}}";
const static std::string portPattern="{{external-port}}";
const static std::string slateTokenPattern="{{slate-token}}";
const static std::string slateEndpointPattern="{{slate-endpoint}}";
const static std::string deploymentTemplate=R"(apiVersion: v1
kind: Secret
metadata:
  name: {{name}}-slate-data
type: Opaque
data:
  token: {{slate-token}}
  endpoint: {{slate-endpoint}}
---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: {{name}}
  labels:
    app: {{name}}
spec:
  replicas: 1
  selector:
    matchLabels:
      app: {{name}}
  template:
    metadata:
      labels:
        app: {{name}}
    spec:
      containers:
      - name: {{name}}
        image: slateci/container-ttyd
        command: ["ttyd"]
        args: ["-c","{{auth}}","bash"]
        imagePullPolicy: IfNotPresent
        ports:
        - containerPort: 7681
          name: ttyd
        volumeMounts:
        - name: slate-data
          mountPath: "/home/slate/.slate"
      volumes:
      - name: slate-data
        secret:
          secretName: {{name}}-slate-data
---
kind: Service
apiVersion: v1
metadata:
  name: {{name}}-service
spec:
  selector:
    app: {{name}}
  type: "LoadBalancer"
  ports:
  - protocol: TCP
    port: {{external-port}} # external port
    targetPort: 7681 # internal port where the daemon is listening
)";

//this should be done with regular expressions but gcc 4.8 is too broken
void replaceAll(std::string& base, const std::string& target, const std::string& replacement){
	std::size_t pos=0;
	while((pos=base.find(target,pos))!=std::string::npos)
		base.replace(pos,target.size(),replacement);
}

crow::response createAccount(DataStore& store, const crow::request& req, const std::string globusID){
	auto account=store.find(globusID);
	
	if(!account){ //create account if it does not exist
		std::cout << "creating account " << globusID << std::endl;
		//make a blank object
		account=UserData{};
		//generate an authentication token
		account->authToken="slate:"+tokenGenerator.getToken();
		account->servicePort=store.getPort();
		//create the acount in SLATE
		std::cout << "Creating SLATE account" << std::endl;
		rapidjson::Document request(rapidjson::kObjectType);
		rapidjson::Document::AllocatorType& alloc = request.GetAllocator();
		request.AddMember("version", "v1alpha1", alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", globusID, alloc);
		metadata.AddMember("email", "-", alloc);
		metadata.AddMember("globusID", globusID, alloc);
		metadata.AddMember("admin", false, alloc);
		request.AddMember("metadata", metadata, alloc);
		
		std::string url=config.slateEndpoint+"/v1alpha1/users?token="+config.slateAdminToken;
		auto response=httpRequests::httpPost(url,to_string(request));
		if(response.status!=200){
			std::cerr << "Error: " << response.body << std::endl;
			return crow::response(500,generateError("Failed to create SLATE account"));
		}
		
		rapidjson::Document slateData;
		try{
			slateData.Parse(response.body);
		}catch(std::runtime_error& err){
			return crow::response(500,generateError("Unable to parse JSON from SLATE API"));
		}
		account->slateID=slateData["metadata"]["id"].GetString();
		account->slateToken=slateData["metadata"]["access_token"].GetString();
		std::cout << "SLATE ID is " << account->slateID << std::endl;
		//deploy the pod and service in kubernetes
		std::string name="ttyd-"+globusID;
		std::string deployment=deploymentTemplate;
		replaceAll(deployment,namePattern,name);
		replaceAll(deployment,authPattern,account->authToken);
		replaceAll(deployment,portPattern,std::to_string(account->servicePort));
		replaceAll(deployment,slateTokenPattern,base64_encode(account->slateToken.c_str(),account->slateToken.size()));
		replaceAll(deployment,slateEndpointPattern,base64_encode(config.slateEndpoint.c_str(),config.slateEndpoint.size()));
		
		std::cout << "Deploying kubernetes objects" << std::endl;
		{
			std::ofstream tmpfile(globusID);
			tmpfile << deployment;
		}
		//auto applyResult=runCommandWithInput("kubectl",deployment,{"apply","-f",""});
		auto applyResult=runCommand("kubectl",{"apply","-f",globusID});
		remove(globusID.c_str());
		if(applyResult.status!=0){
			std::cerr << applyResult.error << std::endl;
			return crow::response(500,generateError("Unable to deploy kubernetes pod"));
		}
		account->deploymentName=name;
		account->serviceName=name+"-service";
		account->secretName=name+"-slate-data";
		//figure out the name of the pod which was started
		std::cout << "Locating new pod" << std::endl;
		auto podResult=runCommand("kubectl",{"get","pods","-l","app="+name,"-o=jsonpath={.items[*].metadata.name}"});
		if(podResult.status!=0){
			std::cerr << podResult.error << std::endl;
			return crow::response(500,generateError("Unable to look up kubernetes pod"));
		}
		//TODO: deal with possibility of finding more than one pod!
		account->podName=podResult.output;
		store.record(globusID,*account);
	}
	
	rapidjson::Document response(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = response.GetAllocator();
	response.AddMember("auth", account->authToken, alloc);
	return crow::response(to_string(response));
}

crow::response podReady(DataStore& store, const crow::request& req, const std::string globusID){
	std::cout << "checking whether pod is ready for " << globusID << std::endl;
	auto pod=store.find(globusID);
	if(!pod)
		return crow::response(404,generateError("User not found"));
	auto result=runCommand("kubectl",{"get","pod",pod->podName,"-o=json"});
	if(result.status!=0)
		return crow::response(500,generateError("kubectl get pod failed: "+result.error));
	rapidjson::Document data;
	try{
		data.Parse(result.output);
	}catch(std::runtime_error& err){
		return crow::response(500,generateError("Unable to parse JSON from kubectl"));
	}
	if(data.IsNull() || !data.HasMember("status") || !data["status"].IsObject()
	   || !data["status"].HasMember("conditions") || !data["status"]["conditions"].IsArray())
		return crow::response(500,generateError("Unable to parse JSON from kubectl"));
	bool ready=false;
	for(const auto& condition : data["status"]["conditions"].GetArray()){
		if(condition["type"].GetString()==std::string("Ready")){
			//for some reason this is a string?
			std::string readyString=condition["status"].GetString();
			ready=(readyString=="True");
			break;
		}
	}
	
	rapidjson::Document response(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = response.GetAllocator();
	response.AddMember("ready", rapidjson::Value(ready), alloc);
	return crow::response(to_string(response));
}

crow::response serviceDetails(DataStore& store, const crow::request& req, const std::string globusID){
	std::cout << "getting service endpoint for " << globusID << std::endl;
	auto acount=store.find(globusID);
	if(!acount)
		return crow::response(404,generateError("User not found"));
	auto result=runCommand("kubectl",{"get","service",acount->serviceName,"-o=json"});
	if(result.status!=0)
		return crow::response(500,generateError("kubectl get service failed: "+result.error));
	rapidjson::Document data;
	try{
		data.Parse(result.output);
	}catch(std::runtime_error& err){
		return crow::response(500,generateError("Unable to parse JSON from kubectl"));
	}
	
	if(data.IsNull() || !data.HasMember("status") || !data["status"].IsObject()
	   || !data["status"].HasMember("loadBalancer") || !data["status"]["loadBalancer"].IsObject()
	   || !data["status"]["loadBalancer"].HasMember("ingress") || !data["status"]["loadBalancer"]["ingress"].IsArray())
		return crow::response(500,generateError("Unable to parse JSON from kubectl"));
	std::string externalIP=data["status"]["loadBalancer"]["ingress"][0]["ip"].GetString();
	std::string port=std::to_string(data["spec"]["ports"][0]["port"].GetInt());
	
	rapidjson::Document response(rapidjson::kObjectType);
	rapidjson::Document::AllocatorType& alloc = response.GetAllocator();
	response.AddMember("endpoint", externalIP+":"+port, alloc);
	return crow::response(to_string(response));
}

crow::response deleteAccount(DataStore& store, const crow::request& req, const std::string globusID){
	std::cout << "deleting account " << globusID << std::endl;
	auto account=store.find(globusID);
	if(!account)
		return crow::response(404,generateError("User not found"));
	
	//delete the corresponding SLATE account
	std::string url=config.slateEndpoint+"/v1alpha1/users/"+account->slateID+"?token="+config.slateAdminToken;
	auto response=httpRequests::httpDelete(url);
	if(response.status!=200){
		std::cerr << "Error: " << response.body << std::endl;
		return crow::response(500,generateError("Failed to delete SLATE account"));
	}
	//delete the deployment
	auto result=runCommand("kubectl",{"delete","deployment",account->deploymentName});
	if(result.status!=0)
		return crow::response(500,generateError("kubectl delete deployment failed: "+result.error));
	//delete the service
	result=runCommand("kubectl",{"delete","service",account->serviceName});
	if(result.status!=0)
		return crow::response(500,generateError("kubectl delete service failed: "+result.error));
	//delete the secret
	result=runCommand("kubectl",{"delete","secret",account->secretName});
	if(result.status!=0)
		return crow::response(500,generateError("kubectl delete secret failed: "+result.error));
	
	return crow::response(200);
}

int main(){
	DataStore store("data");
	
	unsigned int port=0;
	{
		std::istringstream is(config.portString);
		is >> port;
		if(!port || is.fail()){
			std::cerr << "Unable to parse \"" << config.portString << "\" as a valid port number";
			return 1;
		}
	}
	
	crow::SimpleApp server;
	
	CROW_ROUTE(server, "/account/<string>").methods("PUT"_method)(
	  [&](const crow::request& req, std::string globusID){ return createAccount(store,req,globusID); });
	CROW_ROUTE(server, "/account/<string>").methods("DELETE"_method)(
	  [&](const crow::request& req, std::string globusID){ return deleteAccount(store,req,globusID); });
	CROW_ROUTE(server, "/pod_ready/<string>").methods("GET"_method)(
	  [&](const crow::request& req, std::string globusID){ return podReady(store,req,globusID); });
	CROW_ROUTE(server, "/service/<string>").methods("GET"_method)(
	  [&](const crow::request& req, std::string globusID){ return serviceDetails(store,req,globusID); });
	
	startReaper();
	server.loglevel(crow::LogLevel::Warning);
	if(!config.sslCertificate.empty())
		server.port(port).ssl_file(config.sslCertificate,config.sslKey).multithreaded().run();
	else
		server.port(port).multithreaded().run();
}
