# Vagrant configuration for ASSS
#
# Guide:
# 1) Install VirtualBox https://www.virtualbox.org/ (or another provider that vagrant supports)
# 2) Install Vagrant: http://www.vagrantup.com/
# 3) Run "vagrant up" in this directory to set up the VM
# 4) Run "vagrant ssh" to login
# 5) "cd /zone" and "bin/asss" to run the server! 
# Anytime you change the source and you would like to rebuild, run "vagrant provision"
#

Vagrant.configure('2') do |config|
	config.vm.box = 'ubuntu/trusty64'
	config.vm.box_url = 'http://cloud-images.ubuntu.com/vagrant/trusty/current/trusty-server-cloudimg-amd64-vagrant-disk1.box'
	config.vm.network :forwarded_port, guest: 5000, host: 5000, protocol: "udp"
	config.vm.network :forwarded_port, guest: 5000, host: 5000, protocol: "tcp"
	config.vm.network :forwarded_port, guest: 5001, host: 5001, protocol: "udp"

	config.vm.provision 'shell', inline: <<-SHELL
		let "UPDATE_AGO = $(date +%s) - $(date -r ~/last-apt-update +%s)"
		if [ ! -f ~/last-apt-update ] || [ $UPDATE_AGO -gt 86400 ]; then
			apt-get update
			touch ~/last-apt-update
			apt-get install -y build-essential python2.7 python2.7-dev python2.7-dbg \
			libdb5.3-dev mysql-client libmysqlclient-dev gdb mercurial wget nano
		fi

		if [ ! -f /root/.bash_aliases ]; then
			echo "alias src='cd /asss/src'" > /root/.bash_aliases
			echo "alias zone='cd /zone'" >> /root/.bash_aliases
		fi

		if [ ! -f /home/vagrant/.bash_aliases ]; then
			echo "alias src='cd /asss/src'" > /home/vagrant/.bash_aliases
			echo "alias zone='cd /zone'" >> /home/vagrant/.bash_aliases
			echo "alias runzone='cd /zone && bin/asss'" >> /home/vagrant/.bash_aliases
		fi
		
		rsync -rt /vagrant/ /asss/
		mkdir -p /asss/bin
		chown -R vagrant:vagrant /asss/
		
		if [ ! -f /asss/src/system.mk ]; then
			cp /asss/src/system.mk.trusty.dist /asss/src/system.mk
		fi
		
		if [ ! -d /zone ]; then
			cp -R /asss/dist /zone
			chown -R vagrant:vagrant /zone
			ln -s /asss/bin /zone/bin
		fi
		
		if [ ! -f /asss/bin/enc_cont.so ]; then
			wget --output-document=/asss/bin/enc_cont.so https://bitbucket.org/grelminar/asss/downloads/enc_cont_1.6.0_libc2.11.1_64bit.so
			chown vagrant:vagrant /asss/bin/enc_cont.so
		fi
		
		cd /asss/src/
		sudo -u vagrant make deps
		sudo -u vagrant make
	SHELL
end
