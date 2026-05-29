#!/bin/sh

cache_dir=/var/cache/cgit
cgi_dir=/srv/www/cgit
cgi_user=webrunner
cgi_group=webrunner

rm -f ${cgi_dir}/cgit.cgi-nope

if [ -d ${cache_dir} ] ; then
	if [ -f ${cgi_dir}/cgit.cgi ] ; then
		mv ${cgi_dir}/cgit.cgi ${cgi_dir}/cgit.cgi-nope
		echo "Sleeping 7s to end all cgit processes"
		sleep 7s
	fi
	rm -rf ${cache_dir}
fi

mkdir ${cache_dir}
chown -R ${cgi_user}:${cgi_group} ${cache_dir}

if [ -f ${cgi_dir}/cgit.cgi-nope ] ; then
	mv ${cgi_dir}/cgit.cgi-nope ${cgi_dir}/cgit.cgi
fi
