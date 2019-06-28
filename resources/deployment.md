Docker, Kubernetes, etc
======================

```
yum update -y

reboot

yum install docker -y
systemctl start docker

cat <<EOF > /etc/yum.repos.d/kubernetes.repo
[kubernetes]
name=Kubernetes
baseurl=https://packages.cloud.google.com/yum/repos/kubernetes-el7-x86_64
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=https://packages.cloud.google.com/yum/doc/yum-key.gpg https://packages.cloud.google.com/yum/doc/rpm-package-key.gpg
exclude=kube*
EOF

# Set SELinux in permissive mode (effectively disabling it)
setenforce 0
sed -i 's/^SELINUX=enforcing$/SELINUX=permissive/' /etc/selinux/config

yum install -y kubelet kubeadm kubectl --disableexcludes=kubernetes

systemctl enable kubelet && systemctl start kubelet

kubeadm init --pod-network-cidr=192.168.0.0/16

export KUBECONFIG=/etc/kubernetes/admin.conf 

kubectl apply -f https://docs.projectcalico.org/v3.1/getting-started/kubernetes/installation/hosted/rbac-kdd.yaml
kubectl apply -f https://docs.projectcalico.org/v3.1/getting-started/kubernetes/installation/hosted/kubernetes-datastore/calico-networking/1.7/calico.yaml
kubectl taint nodes --all node-role.kubernetes.io/master-
```

DynamoDB
==================================
```
adduser slate
mkdir /opt/dynamodb
cd /opt/dynamodb
curl -O https://s3-us-west-2.amazonaws.com/dynamodb-local/dynamodb_local_latest.tar.gz
tar -xvzf dynamodb_local_latest.tar.gz
yum install java -y

cat << EOF > /etc/systemd/system/dynamodb.service
[Unit]
Description=DynamoDB
After=syslog.target network.target

[Service]
Type=simple
User=slate
WorkingDirectory=/opt/dynamodb
ExecStart=/bin/java -Djava.library.path=./DynamoDBLocal_lib -jar DynamoDBLocal.jar -sharedDb
Restart=always

[Install]
WantedBy=multi-user.target
EOF

chown -R slate: /opt/dynamodb
systemctl daemon-reload
systemctl start dynamodb
```


SLATE API Server
====================
```
cat << EOF > /etc/yum.repos.d/aws-sdk.repo
[aws-sdk]
name=AWS SDK for C++
baseurl=https://jenkins.slateci.io/artifacts/static
enabled=1
gpgcheck=0
repo_gpgcheck=0
EOF

cat << EOF > /etc/yum.repos.d/slate-server.repo
[slate-server]
name=SLATE-server
baseurl=http://jenkins.slateci.io/artifacts/server
enabled=1
gpgcheck=0
repo_gpgcheck=0
EOF
yum install slate-api-server -y

mkdir /opt/slate-api-server
cd /opt/slate-api-server
cat << EOF > slate_portal_user
User_12345678-9abc-def0-1234-56789abcdef0
WebPortal
admin@slateci.io
555-555-5555
SLATE
3dc7cbbd-098b-4fff-9957-047883da78a7
EOF

dd if=/dev/random of=encryption-key bs=1K count=1

chown -R slate: /opt/slate-api-server

mkdir /tmp/helm
cd /tmp/helm
curl -O https://storage.googleapis.com/kubernetes-helm/helm-v2.11.0-linux-amd64.tar.gz
tar -xvzf helm-v2.11.0-linux-amd64.tar.gz
mv linux-amd64/helm linux-amd64/tiller /usr/local/bin

cat << EOF > /etc/systemd/system/slate-api-server.service
[Unit]
Description=SLATE API Server
After=syslog.target network.target dynamodb.service

[Service]
User=slate
Type=simple
WorkingDirectory=/opt/slate-api-server
ExecStart=/usr/bin/slate-service --encryptionKeyFile encryption-key
Restart=always

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl start slate-api-server
```


Sandbox Spawner
=================
```
yum install http://jenkins.slateci.io/artifacts/sandbox-spawner-0.1.0-1.el7.x86_64.rpm
mkdir /opt/sandbox-spawner
cat << EOF > /etc/systemd/system/sandbox-spawner.service
[Unit]
Description=Sandbox Spawner
After=syslog.target network.target slate-api-server.service

[Service]
Type=simple
User=slate
Environment=KUBECONFIG=/etc/kubernetes/admin.conf
WorkingDirectory=/opt/sandbox-spawner
ExecStart=/usr/bin/sandbox-spawner
Restart=always

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl start sandbox-spawner
```

Portal
=======
```
yum install nginx certbot certbot-nginx python-virtualenv git uwsgi uwsgi-plugin-python2
systemctl start nginx
certbot --nginx
cd /opt
git clone https://github.com/slateci/sandbox-portal
cd sandbox-portal
virtualenv venv
source venv/bin/activate
pip install -r requirements.txt

cat << EOF > /opt/sandbox-portal/sandbox-portal-uwsgi.ini
[uwsgi]
socket = :3031
master = true
processes = 4
plugins = python
venv = /opt/sandbox-portal/venv
user = centos
chdir = /opt/sandbox-portal
module = run_portal
callable = app
wsgi_file = /opt/sandbox-portal/run_portal.py
logto = /tmp/uwsgi.log
EOF
```

Edit /etc/nginx/nginx.conf to contain the following (with appropriate hostname substitutions):
```
    server {
        listen       80 default_server;
        listen       [::]:80 default_server;
        server_name  _;
        return 301 https://$host$request_uri;
    }

    server {
        listen       443 ssl http2 default_server;
        listen       [::]:443 ssl http2 default_server;
        server_name  sandbox.slateci.io;
        root         /usr/share/nginx/html;

        ssl_certificate "/etc/letsencrypt/live/sandbox.slateci.io/cert.pem";
        ssl_certificate_key "/etc/letsencrypt/live/sandbox.slateci.io/privkey.pem";
        ssl_session_cache shared:SSL:1m;
        ssl_session_timeout  10m;
        ssl_ciphers HIGH:!aNULL:!MD5;
        ssl_prefer_server_ciphers on;

        # Load configuration files for the default server block.
        include /etc/nginx/default.d/*.conf;

        location / {
            include uwsgi_params;
            uwsgi_pass 127.0.0.1:3031;
        }

        error_page 404 /404.html;
            location = /40x.html {
        }

        error_page 500 502 503 504 /50x.html;
            location = /50x.html {
        }
    }
```

```
cat << EOF > /etc/nginx/uwsgi_params
uwsgi_param  QUERY_STRING       $query_string;
uwsgi_param  REQUEST_METHOD     $request_method;
uwsgi_param  CONTENT_TYPE       $content_type;
uwsgi_param  CONTENT_LENGTH     $content_length;

uwsgi_param  REQUEST_URI        $request_uri;
uwsgi_param  PATH_INFO          $document_uri;
uwsgi_param  DOCUMENT_ROOT      $document_root;
uwsgi_param  SERVER_PROTOCOL    $server_protocol;
uwsgi_param  REQUEST_SCHEME     $scheme;
uwsgi_param  HTTPS              $https if_not_empty;

uwsgi_param  REMOTE_ADDR        $remote_addr;
uwsgi_param  REMOTE_PORT        $remote_port;
uwsgi_param  SERVER_PORT        $server_port;
uwsgi_param  SERVER_NAME        $server_name;
EOF

cat << EOF > /etc/systemd/system/nginx.service
[Unit]
Description=Nginx
After=syslog.target network.target uwsgi.service

[Service]
Type=simple
User=root
ExecStart=/usr/sbin/nginx -g "daemon off;"
Restart=always

[Install]
WantedBy=multi-user.target
EOF

cat << EOF > /etc/systemd/system/sandbox-portal.service
[Unit]
Description=uWSGI
After=syslog.target network.target sandbox-spawner.service

[Service]
Type=simple
User=slate
ExecStart=/usr/sbin/uwsgi --ini /opt/sandbox-portal/sandbox-portal-uwsgi.ini --die-on-term
Restart=always

[Install]
WantedBy=multi-user.target
EOF

```

Edit /opt/sandbox-portal/portal/portal.conf to use the correct `SERVER_NAME`, `PORTAL_CLIENT_ID`, and `PORTAL_CLIENT_SECRET`. 

Edit run_portal.py and change `localhost` to your DNS name. Then start:

```
systemctl daemon-reload
systemctl start sandbox-portal
systemctl restart nginx
```

Register cluster with SLATE API Server
=====================================
as user `centos`
```
sudo cp /etc/kubernetes/admin.conf .kube/config
sudo chown centos: ./kube/config
mkdir ~/.slate
tail -n1 /opt/slate-api-server/slate_portal_user > ~/.slate/token
curl -LO http://jenkins.slateci.io/artifacts/client/slate-linux.tar.gz
tar xzf slate-linux.tar.gz
rm slate-linux.tar.gz
sudo cp slate /usr/local/bin/slate
sudo mv slate /opt/sandbox-spawner/slate
chmod 0600 .slate/token
./slate vo create slate
./slate cluster create --vo slate slate-tutorial
./slate cluster allow-vo slate-tutorial \*
```

Testing
=========
```
kubectl create namespace tutorial
export SPAWNER_URL=localhost:18081
export GLOBUS_ID=$(uuidgen)

# create an account (returns 200+no body on success)
curl -X PUT -d '' $SPAWNER_URL/account/$GLOBUS_ID
# check whether a pod is ready (returns 200+JSON body with a `ready` attribute on success)
curl $SPAWNER_URL/pod_ready/$GLOBUS_ID
# get service IP/port (returns 200+JSON body with a `url` attribute on success)
curl $SPAWNER_URL/service/$GLOBUS_ID

# delete an account (returns 200+no body on success)
curl -X DELETE $SPAWNER_URL/account/$GLOBUS_ID
```



HTTPS Renewal
======
```
sudo certbot renew

sudo kubectl --kubeconfig /etc/kubernetes/admin.conf delete secret server-certificate -n tutorial
sudo kubectl --kubeconfig /etc/kubernetes/admin.conf create secret generic server-certificate --from-file cert1.pem=/etc/letsencrypt/live/`hostname`/cert.pem --from-file chain1.pem=/etc/letsencrypt/live/`hostname`/chain.pem --from-file fullchain1.pem=/etc/letsencrypt/live/`hostname`/fullchain.pem --from-file privkey1.pem=/etc/letsencrypt/live/`hostname`/privkey.pem -n tutorial
sudo systemctl restart sandbox-portal
```

