#!/bin/csh 


set MW_BUILDPATH=/usr/m0n0wall/build90
setenv MW_BUILDPATH $MW_BUILDPATH
setenv MW_ARCH `uname -m`

# ensure prerequisite tools are installed
if ( ! -x /usr/local/bin/bash ) then
	pkg_add -r bash
endif
if ( ! -x /usr/local/bin/svn ) then
	pkg_add -r subversion
endif

# figure out if we're already running from within a repository
set svninfo=`/usr/local/bin/svn info freebsd9 >& /dev/null`
if  ( $status != 1 ) then
	echo "Found existing working copy"
else
	echo "No working copy found; checking out current version from repository"
	/usr/local/bin/svn checkout http://m0n0wall190.googlecode.com/svn/trunk/freebsd9
endif

cd freebsd9

echo "Creating build directory $MW_BUILDPATH."
mkdir -p $MW_BUILDPATH

echo "Exporting repository to $MW_BUILDPATH/freebsd9."
/usr/local/bin/svn export --force . $MW_BUILDPATH/freebsd9
/usr/local/bin/svnversion -n . > $MW_BUILDPATH/freebsd9/svnrevision

echo "Changing directory to $MW_BUILDPATH/freebsd9/build/scripts"
cd $MW_BUILDPATH/freebsd9/build/scripts
chmod +x *.sh

echo 
echo "----- Build environment prepared -----"
echo "I will leave you in a bash shell now"
echo "To start the build, execute doall.sh or run 1makebuildenv.sh , then 2makebinaries.sh, then 3patchtools.sh etc"
setenv PS1 "m0n0wall-build# "
/usr/local/bin/bash
