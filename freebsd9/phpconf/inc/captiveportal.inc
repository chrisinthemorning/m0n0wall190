<?php
/*
    $Id: captiveportal.inc 450 2011-05-04 11:31:08Z mkasper $
    part of m0n0wall (http://m0n0.ch/wall)
    
    Copyright (C) 2003-2007 Manuel Kasper <mk@neon1.net>.
    All rights reserved.
    
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    
    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    
    THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
    AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
    OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    This version of captiveportal.inc has been modified by Rob Parker
    <rob.parker@keycom.co.uk> to include changes for per-user bandwidth management
    via returned RADIUS attributes. This page has been modified to delete any
    added rules which may have been created by other per-user code (index.php, etc).
    These changes are (c) 2004 Keycom PLC.
*/
    
/* include all configuration functions */
require_once("functions.inc");
require_once("radius.inc");

$lockfile = "{$g['varrun_path']}/captiveportal.lock";

function captiveportal_configure() {
    global $config, $g;
    
    if (isset($config['captiveportal']['enable']) &&
        (($config['captiveportal']['interface'] == "lan") ||
            isset($config['interfaces'][$config['captiveportal']['interface']]['enable']))) {
    
        if ($g['booting'])
            echo "Starting captive portal... ";
        
        /* kill any running mini_httpd */
        killbypid("{$g['varrun_path']}/mini_httpd.cp.pid");
        killbypid("{$g['varrun_path']}/mini_httpd.cps.pid");
        
        /* kill any running minicron */
        killbypid("{$g['varrun_path']}/minicron.pid");
        
        /* generate ipfw rules */
        $cprules = captiveportal_rules_generate();
        
        /* make sure ipfw is loaded */
        mwexec("/sbin/kldload ipfw");
        
        /* stop accounting on all clients */
        captiveportal_radius_stop_all();

        /* initialize minicron interval value */
        $croninterval = $config['captiveportal']['croninterval'] ? $config['captiveportal']['croninterval'] : 60;

        /* double check if the $croninterval is numeric and at least 10 seconds. If not we set it to 60 to avoid problems */
        if ((!is_numeric($croninterval)) || ($croninterval < 10)) { $croninterval = 60; }

        /* remove old information */
        unlink_if_exists("{$g['vardb_path']}/captiveportal.db");
        unlink_if_exists("{$g['vardb_path']}/captiveportal_mac.db");
        unlink_if_exists("{$g['vardb_path']}/captiveportal_ip.db");
        unlink_if_exists("{$g['vardb_path']}/captiveportal_radius.db");
        
        /* write portal page */
        if ($config['captiveportal']['page']['htmltext'])
            $htmltext = base64_decode($config['captiveportal']['page']['htmltext']);
        else {
            /* example/template page */
            $htmltext = <<<EOD
<html>
<head>
<title>m0n0wall captive portal</title>
</head>
<body>
<h2>m0n0wall captive portal</h2>
<p>This is the default captive portal page. Please upload your own custom HTML file on the <em>Services: Captive portal</em> screen in the m0n0wall webGUI.</p>
<form method="post" action="\$PORTAL_ACTION\$">
  <input name="accept" type="submit" value="Continue">
  <input name="redirurl" type="hidden" value="\$PORTAL_REDIRURL\$">
</form>
</body>
</html>

EOD;
        }

        $fd = @fopen("{$g['varetc_path']}/captiveportal.html", "w");
        if ($fd) {
            fwrite($fd, $htmltext);
            fclose($fd);    
        }
        
        /* write error page */
        if ($config['captiveportal']['page']['errtext'])
            $errtext = base64_decode($config['captiveportal']['page']['errtext']);
        else {
            /* example page */
            $errtext = <<<EOD
<html>
<head>
<title>Authentication error</title>
</head>
<body>
<font color="#cc0000"><h2>Authentication error</h2></font>
<b>
Username and/or password invalid.
<br><br>
<a href="javascript:history.back()">Go back</a>
</b>
</body>
</html>

EOD;
        }

        $fd = @fopen("{$g['varetc_path']}/captiveportal-error.html", "w");
        if ($fd) {
            fwrite($fd, $errtext);
            fclose($fd);    
        }
        
        /* write status page */
        if ($config['captiveportal']['page']['statustext'])
            $statustext = base64_decode($config['captiveportal']['page']['statustext']);
        else {
            /* example page with just logout, no password change */
            $statustext = <<<EOD
<html>
<head><title>Wireless session information</title></head>
<body>
<b>You are currently connected</b>
<p><form method="post" action="\$PORTAL_ACTION\$">
<input name="logout_id" type="hidden" value="\$PORTAL_SESSIONID\$">
<input name="logout_info" type="submit" value="Logout">
</form>
</body>
</html>
EOD;
        }

        $fd = @fopen("{$g['varetc_path']}/captiveportal-status.html", "w");
        if ($fd) {
            fwrite($fd, $statustext);
            fclose($fd);
        }

        /* write logout page */
        if ($config['captiveportal']['page']['logouttext'])
            $logouttext = base64_decode($config['captiveportal']['page']['logouttext']);
        else {
            /* example page */
            $logouttext = <<<EOD
<html>
<head><title>Disconnected</title></head>
<body>
<b>You've been disconnected.</b>
<p><a href="/">Log in</a>
</body>
</html>
EOD;
        }

        $fd = @fopen("{$g['varetc_path']}/captiveportal-logout.html", "w");
        if ($fd) {
            fwrite($fd, $logouttext);
            fclose($fd);
        }

        /* write elements */
        captiveportal_write_elements();

        /* load rules */
        mwexec("/sbin/ipfw -f delete set 1");
        mwexec("/sbin/ipfw -f delete set 2");
        mwexec("/sbin/ipfw -f delete set 3");
        
        /* XXX - seems like ipfw cannot accept rules directly on stdin,
           so we have to write them to a temporary file first */
        $fd = @fopen("{$g['tmp_path']}/ipfw.cp.rules", "w");
        if (!$fd) {
            printf("Cannot open ipfw.cp.rules in captiveportal_configure()\n");
            return 1;
        }
            
        fwrite($fd, $cprules);
        fclose($fd);
        
        mwexec("/sbin/ipfw {$g['tmp_path']}/ipfw.cp.rules");
        
        unlink("{$g['tmp_path']}/ipfw.cp.rules");
        
        /* filter on layer2 as well so we can check MAC addresses */
        mwexec("/sbin/sysctl net.link.ether.ipfw=1");
        
        chdir($g['captiveportal_path']);
        
        if ($config['captiveportal']['maxproc'])
            $maxproc = $config['captiveportal']['maxproc'];
        else
            $maxproc = 16;
            
        if (isset($config['captiveportal']['maxprocperip']) &&
                $config['captiveportal']['maxprocperip'] !== "") {
            if ($config['captiveportal']['maxprocperip'] == 0)
                $maxperiparg = "";
            else
                $maxperiparg = "-maxperip " . $config['captiveportal']['maxprocperip'];
        } else
            $maxperiparg = "-maxperip 4";
        
        /* start web server */
        $cpip = $config['interfaces'][$config['captiveportal']['interface']]['ipaddr'];
        mwexec("/usr/local/sbin/mini_httpd -a -M 0 -u root -maxproc $maxproc $maxperiparg" .
            " -p 8000 -i {$g['varrun_path']}/mini_httpd.cp.pid" .
            " -cpelement {$g['captiveportal_element_path']} $cpip:8000");
        
        /* fire up another one for HTTPS if requested */
        if (isset($config['captiveportal']['httpslogin']) &&
            $config['captiveportal']['certificate'] && $config['captiveportal']['private-key']) {
            
            $cert = base64_decode($config['captiveportal']['certificate']);
            $key = base64_decode($config['captiveportal']['private-key']);
            
            $fd = fopen("{$g['varetc_path']}/cert-portal.pem", "w");
            if (!$fd) {
                printf("Error: cannot open cert-portal.pem in system_webgui_start().\n");
                return 1;
            }
            chmod("{$g['varetc_path']}/cert-portal.pem", 0600);
            fwrite($fd, $cert);
            fwrite($fd, "\n");
            fwrite($fd, $key);
            fclose($fd);

            $httpsname = ($config['captiveportal']['httpsname']) ? $config['captiveportal']['httpsname'] : $cpip;

            mwexec("/usr/local/sbin/mini_httpd -S -a -M 0 -E {$g['varetc_path']}/cert-portal.pem" .
                " -u root -maxproc $maxproc $maxperiparg -p 8001" .
                " -i {$g['varrun_path']}/mini_httpd.cps.pid" .
                " -cpelement {$g['captiveportal_element_path']} $httpsname:8001");
        }
            
        /* start pruning process (interval defaults to 60 seconds) */
        mwexec("/usr/local/bin/minicron $croninterval {$g['varrun_path']}/minicron.pid " .
            "/etc/rc.prunecaptiveportal");
        
        /* generate passthru mac database */
        captiveportal_passthrumac_configure();
        /* create allowed ip database and insert ipfw rules to make it so */
        captiveportal_allowedip_configure();

        /* generate radius server database */
        if ($config['captiveportal']['radiusip'] && (!isset($config['captiveportal']['auth_method']) ||
                ($config['captiveportal']['auth_method'] == "radius"))) {
            $radiusip = $config['captiveportal']['radiusip'];
            $radiusip2 = ($config['captiveportal']['radiusip2']) ? $config['captiveportal']['radiusip2'] : null;

            if ($config['captiveportal']['radiusport'])
                $radiusport = $config['captiveportal']['radiusport'];
            else
                $radiusport = 1812;

            if ($config['captiveportal']['radiusacctport'])
                $radiusacctport = $config['captiveportal']['radiusacctport'];
            else
                $radiusacctport = 1813;

            if ($config['captiveportal']['radiusport2'])
                $radiusport2 = $config['captiveportal']['radiusport2'];
            else
                $radiusport2 = 1812;

            $radiuskey = $config['captiveportal']['radiuskey'];
            $radiuskey2 = ($config['captiveportal']['radiuskey2']) ? $config['captiveportal']['radiuskey2'] : null;

            $fd = @fopen("{$g['vardb_path']}/captiveportal_radius.db", "w");
            if (!$fd) {
                printf("Error: cannot open radius DB file in captiveportal_configure().\n");
                return 1;
            } else if (isset($radiusip2, $radiuskey2)) {
                fwrite($fd,$radiusip . "," . $radiusport . "," . $radiusacctport . "," . $radiuskey . "\n"
                     . $radiusip2 . "," . $radiusport2 . "," . $radiusacctport . "," . $radiuskey2);
            } else {
                fwrite($fd,$radiusip . "," . $radiusport . "," . $radiusacctport . "," . $radiuskey);
            }
            fclose($fd);
        }

        if ($g['booting'])
            echo "done\n";
        
    } else {
        killbypid("{$g['varrun_path']}/mini_httpd.cp.pid");
        killbypid("{$g['varrun_path']}/mini_httpd.cps.pid");
        killbypid("{$g['varrun_path']}/minicron.pid");

        captiveportal_radius_stop_all();

        mwexec("/sbin/sysctl net.link.ether.ipfw=0");

        if (!isset($config['shaper']['enable'])) {
            /* unload ipfw */
            mwexec("/sbin/kldunload ipfw");
        } else {
            /* shaper is on - just remove our rules */
            mwexec("/sbin/ipfw -f delete set 1");
            mwexec("/sbin/ipfw -f delete set 2");
            mwexec("/sbin/ipfw -f delete set 3");
        }
    }
    
    return 0;
}

function captiveportal_rules_generate() {
    global $config, $g;
    
    $cpifn = $config['captiveportal']['interface'];
    $cpif = $config['interfaces'][$cpifn]['if'];
    $cpip = $config['interfaces'][$cpifn]['ipaddr'];

    /* note: the captive portal daemon inserts all pass rules for authenticated
       clients as skipto 50000 rules to make traffic shaping work */

    $cprules = "";
    
    /* captive portal on LAN interface? */
    if ($cpifn == "lan") {
        /* add anti-lockout rules */
        $cprules .= <<<EOD
add 500 set 1 pass all from $cpip to any out via $cpif
add 501 set 1 pass all from any to $cpip in via $cpif

EOD;
    }

    $cprules .= <<<EOD
# skip to traffic shaper if not on captive portal interface
add 1000 set 1 skipto 50000 all from any to any not layer2 not via $cpif
# pass all layer2 traffic on other interfaces
add 1001 set 1 pass layer2 not via $cpif
# pass IPv6 since the captive portal doesn't support it yet
add 1002 set 1 skipto 50000 ipv6 from any to any not layer2

# layer 2: pass ARP
add 1100 set 1 pass layer2 mac-type arp
# layer 2: pass IPv6
add 1101 set 1 pass layer2 mac-type ipv6
# layer 2: block anything else non-IP
add 1102 set 1 deny layer2 not mac-type ip
# layer 2: check if MAC addresses of authenticated clients are correct
add 1103 set 1 skipto 20000 layer2

# allow access to our DHCP server (which needs to be able to ping clients as well)
add 1200 set 1 pass udp from any 68 to 255.255.255.255 67 in
add 1201 set 1 pass udp from any 68 to $cpip 67 in
add 1202 set 1 pass udp from $cpip 67 to any 68 out
add 1203 set 1 pass icmp from $cpip to any out icmptype 8
add 1204 set 1 pass icmp from any to $cpip in icmptype 0

# allow access to our DNS forwarder
add 1300 set 1 pass udp from any to $cpip 53 in
add 1301 set 1 pass udp from $cpip 53 to any out

# allow access to our web server
add 1302 set 1 pass tcp from any to $cpip 8000 in
add 1303 set 1 pass tcp from $cpip 8000 to any out

EOD;

    if (isset($config['captiveportal']['httpslogin'])) {
        $cprules .= <<<EOD
add 1304 set 1 pass tcp from any to $cpip 8001 in
add 1305 set 1 pass tcp from $cpip 8001 to any out

## REDIR SSL
# redirect non-authenticated clients to captive portal on ssl
add 19900 set 1 fwd 127.0.0.1,8001 tcp from any to any 443 in

# let the responses from the captive portal webserver back out
add 19901 set 1 pass tcp from any 443 to any out

EOD;
    }

    if (isset($config['captiveportal']['pppoeaccess'])) {
        $cprules .= <<<EOD
#PPPoE Discovery Stage
add 1100 set 1 pass layer2 mac-type 0x8863
#PPPoE Session Stage
add 1100 set 1 pass layer2 mac-type 0x8864

EOD;
    }

    if (isset($config['captiveportal']['wpaaccess'])) {
        $cprules .= <<<EOD
# Allow WPA
add 1100 set 1 pass layer2 mac-type 0x888e

EOD;
    }

    $cprules .= <<<EOD

# ... 10000-19899: rules per authenticated client go here...

# redirect non-authenticated clients to captive portal
add 19902 set 1 fwd 127.0.0.1,8000 tcp from any to any 80 in
# let the responses from the captive portal web server back out
add 19903 set 1 pass tcp from any 80 to any out
# block everything else
add 19904 set 1 deny all from any to any

# ... 20000-29899: layer2 block rules per authenticated client go here...

# pass everything else on layer2
add 29900 set 1 pass all from any to any layer2

EOD;

    return $cprules;
}

/* remove clients that have been around for longer than the specified amount of time */
/* db file structure: 
timestamp,ipfw_rule_no,clientip,clientmac,username,sessionid,password,session_timeout,idle_timeout,session_terminate_time */

/* (password is in Base64 and only saved when reauthentication is enabled) */
function captiveportal_prune_old() {

    global $g, $config;

    /* check for expired entries */
    if ($config['captiveportal']['timeout'])
        $timeout = $config['captiveportal']['timeout'] * 60;
    else
        $timeout = 0;

    if ($config['captiveportal']['idletimeout'])
        $idletimeout = $config['captiveportal']['idletimeout'] * 60;
    else
        $idletimeout = 0;

    if (!$timeout && !$idletimeout && !isset($config['captiveportal']['reauthenticate']) && !isset($config['captiveportal']['radiussession_timeout']) && !isset($config['voucher']['enable']))
        return;

    captiveportal_lock();

    /* read database */
    $cpdb = captiveportal_read_db();

    $radiusservers = captiveportal_get_radius_servers();

    $no_users = count($cpdb);
    for ($i = 0; $i < $no_users; $i++) {

        $timedout = false;
        $term_cause = 1;

        /* no pruning for fixed mac address entry */
	if (portal_mac_fixed($cpdb[$i][3])) {
		continue; // check next value
	}

        /* hard timeout? */
        if ($timeout) {
            if ((time() - $cpdb[$i][0]) >= $timeout) {
                $timedout = true;
                $term_cause = 5; // Session-Timeout
            }
        }

        /* Session-Terminate-Time */
        if (!$timedout && !empty($cpdb[$i][9])) {
            if (time() >= $cpdb[$i][9]) {
                $timedout = true;
                $term_cause = 5; // Session-Timeout
            }
        }

        /* check if the radius idle_timeout attribute has been set and if its set change the idletimeout to this value */
        $idletimeout = (is_numeric($cpdb[$i][8])) ? $cpdb[$i][8] : $idletimeout;
        /* if an idle timeout is specified, get last activity timestamp from ipfw */
        if (!$timedout && $idletimeout) {
            $lastact = captiveportal_get_last_activity($cpdb[$i][1]);
            /* if the user has logged on but not sent any traffic, they will never be logged out.
               We "fix" this by setting lastact to the login timestamp */
            $lastact = $lastact ? $lastact : $cpdb[$i][0];
            if ($lastact && ((time() - $lastact) >= $idletimeout)) {
                $timedout = true;
                $term_cause = 4; // Idle-Timeout
                $stop_time = $lastact; // Entry added to comply with WISPr
            }
        }

        /* if vouchers are configured, activate session timeouts */
        if (!$timedout && isset($config['voucher']['enable']) && !empty($cpdb[$i][7])) {
            if (time() >= ($cpdb[$i][0] + $cpdb[$i][7])) {
                $timedout = true;
                $term_cause = 5; // Session-Timeout
            }
        }

        /* if radius session_timeout is enabled and the session_timeout is not null, then check if the user should be logged out */
        if (!$timedout && isset($config['captiveportal']['radiussession_timeout']) && !empty($cpdb[$i][7])) {
            if (time() >= ($cpdb[$i][0] + $cpdb[$i][7])) {
                $timedout = true;
                $term_cause = 5; // Session-Timeout
            }
        }

        if ($timedout) {
            captiveportal_disconnect($cpdb[$i], $radiusservers,$term_cause,$stop_time);
            captiveportal_logportalauth($cpdb[$i][4], $cpdb[$i][3], $cpdb[$i][2], "TIMEOUT");
            unset($cpdb[$i]);
        }

        /* do periodic RADIUS reauthentication? */
        if (!$timedout && isset($config['captiveportal']['reauthenticate']) &&
            ($radiusservers !== false)) {

            if (isset($config['captiveportal']['radacct_enable'])) {
                if ($config['captiveportal']['reauthenticateacct'] == "stopstart") {
                    /* stop and restart accounting */
                    RADIUS_ACCOUNTING_STOP($cpdb[$i][1], // ruleno
                                           $cpdb[$i][4], // username
                                           $cpdb[$i][5], // sessionid
                                           $cpdb[$i][0], // start time
                                           $radiusservers[0]['ipaddr'],
                                           $radiusservers[0]['acctport'],
                                           $radiusservers[0]['key'],
                                           $cpdb[$i][2], // clientip
                                           $cpdb[$i][3], // clientmac
                                           10); // NAS Request
                    exec("/sbin/ipfw zero {$cpdb[$i][1]}");
                    RADIUS_ACCOUNTING_START($cpdb[$i][1], // ruleno
                                            $cpdb[$i][4], // username
                                            $cpdb[$i][5], // sessionid
                                            $radiusservers[0]['ipaddr'],
                                            $radiusservers[0]['acctport'],
                                            $radiusservers[0]['key'],
                                            $cpdb[$i][2], // clientip
                                            $cpdb[$i][3]); // clientmac
                } else if ($config['captiveportal']['reauthenticateacct'] == "interimupdate") {
                    RADIUS_ACCOUNTING_STOP($cpdb[$i][1], // ruleno
                                           $cpdb[$i][4], // username
                                           $cpdb[$i][5], // sessionid
                                           $cpdb[$i][0], // start time
                                           $radiusservers[0]['ipaddr'],
                                           $radiusservers[0]['acctport'],
                                           $radiusservers[0]['key'],
                                           $cpdb[$i][2], // clientip
                                           $cpdb[$i][3], // clientmac
                                           10, // NAS Request
                                           true); // Interim Updates
                }
            }

            /* check this user against RADIUS again */
            $auth_list = RADIUS_AUTHENTICATION($cpdb[$i][4], // username
                                          base64_decode($cpdb[$i][6]), // password
                                            $radiusservers,
                                          $cpdb[$i][2], // clientip
                                          $cpdb[$i][3], // clientmac
                                          $cpdb[$i][1]); // ruleno

            if ($auth_list['auth_val'] == 3) {
                captiveportal_disconnect($cpdb[$i], $radiusservers, 17);
                captiveportal_logportalauth($cpdb[$i][4], $cpdb[$i][3], $cpdb[$i][2], "RADIUS_DISCONNECT", $auth_list['reply_message']);
                unset($cpdb[$i]);
            }
        }
    }

    /* write database */
    captiveportal_write_db($cpdb);

    captiveportal_unlock();
}

/* remove a single client according to the DB entry */
function captiveportal_disconnect($dbent, $radiusservers,$term_cause = 1,$stop_time = null) {

    global $g, $config;

    $stop_time = (empty($stop_time)) ? time() : $stop_time;

    /* this client needs to be deleted - remove ipfw rules */
    if (isset($config['captiveportal']['radacct_enable']) && isset($radiusservers[0])) {
        RADIUS_ACCOUNTING_STOP($dbent[1], // ruleno
                               $dbent[4], // username
                               $dbent[5], // sessionid
                               $dbent[0], // start time
                               $radiusservers[0]['ipaddr'],
                               $radiusservers[0]['acctport'],
                               $radiusservers[0]['key'],
                               $dbent[2], // clientip
                               $dbent[3], // clientmac
                               $term_cause, // Acct-Terminate-Cause
                               false,
                               $stop_time);
    }

    mwexec("/sbin/ipfw delete " . $dbent[1] . " " . ($dbent[1]+10000));

    /* We need to delete +40500 and +45500 as well...
     * these are the pipe numbers we use to control traffic shaping for each logged in user via captive portal
     * We could get an error if the pipe doesn't exist but everything should still be fine
     */
    if (isset($config['captiveportal']['peruserbw'])) {
        mwexec("/sbin/ipfw pipe " . ($dbent[1]+40500) . " delete");
        mwexec("/sbin/ipfw pipe " . ($dbent[1]+45500) . " delete");
    }
}

/* remove a single client by ipfw rule number */
function captiveportal_disconnect_client($id,$term_cause = 1) {

    global $g, $config;

    captiveportal_lock();

    /* read database */
    $cpdb = captiveportal_read_db();
    $radiusservers = captiveportal_get_radius_servers();

    /* find entry */
    for ($i = 0; $i < count($cpdb); $i++) {
        if ($cpdb[$i][1] == $id) {
            captiveportal_disconnect($cpdb[$i], $radiusservers, $term_cause);
            captiveportal_logportalauth($cpdb[$i][4], $cpdb[$i][3], $cpdb[$i][2], "DISCONNECT");
            unset($cpdb[$i]);
            break;
        }
    }

    /* write database */
    captiveportal_write_db($cpdb);

    captiveportal_unlock();
}

/* send RADIUS acct stop for all current clients */
function captiveportal_radius_stop_all() {
    global $g, $config;

    if (!isset($config['captiveportal']['radacct_enable']))
        return;

    captiveportal_lock();
    $cpdb = captiveportal_read_db();

    $radiusservers = captiveportal_get_radius_servers();

    if (isset($radiusservers[0])) {
        for ($i = 0; $i < count($cpdb); $i++) {
            RADIUS_ACCOUNTING_STOP($cpdb[$i][1], // ruleno
                                   $cpdb[$i][4], // username
                                   $cpdb[$i][5], // sessionid
                                   $cpdb[$i][0], // start time
                                   $radiusservers[0]['ipaddr'],
                                   $radiusservers[0]['acctport'],
                                   $radiusservers[0]['key'],
                                   $cpdb[$i][2], // clientip
                                   $cpdb[$i][3], // clientmac
                                   7); // Admin Reboot
        }
    }
    captiveportal_unlock();
}

function captiveportal_passthrumac_configure() {
    global $config, $g;

    captiveportal_lock();

    /* clear out passthru macs, if necessary */
    unlink_if_exists("{$g['vardb_path']}/captiveportal_mac.db");

    if (is_array($config['captiveportal']['passthrumac'])) {

        $fd = @fopen("{$g['vardb_path']}/captiveportal_mac.db", "w");
        if (!$fd) {
            printf("Error: cannot open passthru mac DB file in captiveportal_passthrumac_configure().\n");
            captiveportal_unlock();
            return 1;
        }

        foreach ($config['captiveportal']['passthrumac'] as $macent) {
            /* record passthru mac so it can be recognized and let thru */
            fwrite($fd, $macent['mac'] . "\n");
        }

        fclose($fd); 
    }

    captiveportal_unlock();

    return 0;
}

function captiveportal_allowedip_configure() {
    global $config, $g;

    captiveportal_lock();

    /* clear out existing allowed ips, if necessary */
    if (file_exists("{$g['vardb_path']}/captiveportal_ip.db")) {
        $fd = @fopen("{$g['vardb_path']}/captiveportal_ip.db", "r");
        if ($fd) {
            while (!feof($fd)) {
                $line = trim(fgets($fd));
                if ($line) {
                    list($ip,$rule) = explode(",",$line);
                    mwexec("/sbin/ipfw delete $rule");
                }
            }
        }
        fclose($fd);
        unlink("{$g['vardb_path']}/captiveportal_ip.db");
    }

    if (is_array($config['captiveportal']['allowedip'])) {

        $fd = @fopen("{$g['vardb_path']}/captiveportal_ip.db", "w");
        if (!$fd) {
            printf("Error: cannot open allowed ip DB file in captiveportal_allowedip_configure().\n");
            captiveportal_unlock();
            return 1;
        }

        foreach ($config['captiveportal']['allowedip'] as $ipent) {

            /* get next ipfw rule number */
            $ruleno = captiveportal_get_next_ipfw_ruleno();

            /* if the pool is empty, return apprioriate message and fail */
            if (is_null($ruleno)) {
                printf("Error: system reached maximum login capacity, no free FW rulenos in captiveportal_allowedip_configure().\n");
                fclose($fd);
                captiveportal_unlock();
                return 1;
            }

            /* record allowed ip so it can be recognized and removed later */
            fwrite($fd, $ipent['ip'] . "," . $ruleno ."\n");

            /* insert ipfw rule to allow ip thru */
            if ($ipent['dir'] == "from") {
                mwexec("/sbin/ipfw add $ruleno set 2 skipto 50000 ip from " . $ipent['ip'] . " to any in");
                mwexec("/sbin/ipfw add $ruleno set 2 skipto 50000 ip from any to " . $ipent['ip'] . " out");
            } else {
                mwexec("/sbin/ipfw add $ruleno set 2 skipto 50000 ip from any to " . $ipent['ip'] . " in");
                mwexec("/sbin/ipfw add $ruleno set 2 skipto 50000 ip from " . $ipent['ip'] . " to any out");
            }

        }

        fclose($fd);
    }

    captiveportal_unlock();
    return 0;
}

/* get last activity timestamp given ipfw rule number */
function captiveportal_get_last_activity($ruleno) {

    exec("/sbin/ipfw -T list {$ruleno} 2>/dev/null", $ipfwoutput);

    /* in */
    if ($ipfwoutput[0]) {
        $ri = explode(" ", $ipfwoutput[0]);
        if ($ri[1])
            return $ri[1];
    }

    return 0;
}

/* read RADIUS servers into array */
function captiveportal_get_radius_servers() {

        global $g;

        if (file_exists("{$g['vardb_path']}/captiveportal_radius.db")) {
                $fd = @fopen("{$g['vardb_path']}/captiveportal_radius.db","r");
                if ($fd) {
                        $radiusservers = array();
                        while (!feof($fd)) {
                                $line = trim(fgets($fd));
                                if ($line) {
                                        $radsrv = array();
                                        list($radsrv['ipaddr'],$radsrv['port'],$radsrv['acctport'],$radsrv['key']) = explode(",",$line);
                                        $radiusservers[] = $radsrv;
                                }
                        }
                        fclose($fd);

                        return $radiusservers;
                }
        }

        return false;
}

/* lock captive portal information, decide that the lock file is stale after 
   4 minutes, issue warning in syslog every minute */
function captiveportal_lock() {

	global $lockfile;
	
	$starttime = time();
	do {
		/* open the lock file in append mode to avoid race condition */
		if ($fd = @fopen($lockfile, "x")) {
			/* succeeded */
			fclose($fd);
			return;
		} else {
			/* file locked, wait and try again */
			usleep(rand(200000,500000));
			
			$waittime = time() - $starttime;
			
			if ($waittime > 0 && ($waittime % 60) == 0)
				captiveportal_syslog("LOCKWARNING: waited for lock for " . $waittime/60 ." minute(s)");
		}
	} while ($waittime < 240);
	
	captiveportal_syslog("LOCKWARNING: waited for lock for " . $waittime/60 ." minute(s) - breaking lock!");
}

/* unlock captive portal information file */
function captiveportal_unlock() {

        global $lockfile;

        if (file_exists($lockfile))
                unlink($lockfile);
}

/* log successful captive portal authentication to syslog */
/* part of this code from php.net */
function captiveportal_logportalauth($user,$mac,$ip,$status, $message = null) {
	$message = trim($message);
	// Log it
	if (!$message)
		$message = "$status: $user, $mac, $ip";
	else
		$message = "$status: $user, $mac, $ip, $message";
	captiveportal_syslog($message);
}

/* log simple messages to syslog */
function captiveportal_syslog($message) {
    define_syslog_variables();
    $message = trim($message);
    openlog("logportalauth", LOG_PID, LOG_LOCAL4);
    // Log it
    syslog(LOG_INFO, $message);
    closelog();
}

function radius($username,$password,$clientip,$clientmac,$type) {
    global $g, $config;

    /* Start locking from the beginning of an authentication session */
    captiveportal_lock();

    $ruleno = captiveportal_get_next_ipfw_ruleno();

    /* if the pool is empty, return apprioriate message and fail authentication */
    if (is_null($ruleno)) {
        $auth_list = array();
        $auth_list['auth_val'] = 1;
        $auth_list['error'] = "System reached maximum login capacity";
        captiveportal_unlock();
        return $auth_list;
    }

    $radiusservers = captiveportal_get_radius_servers();

    $auth_list = RADIUS_AUTHENTICATION($username,
                    $password,
                    $radiusservers,
                    $clientip,
                    $clientmac,
                    $ruleno);

    if ($auth_list['auth_val'] == 2) {
        captiveportal_logportalauth($username,$clientmac,$clientip,$type);
        $sessionid = portal_allow($clientip,
                    $clientmac,
                    $username,
                    $password,
                    $auth_list,
                    $ruleno);
    }
    else {
        captiveportal_unlock();
    }

    return $auth_list;

}

/* read captive portal DB into array */
function captiveportal_read_db() {

        global $g;

        $cpdb = array();
        $fd = @fopen("{$g['vardb_path']}/captiveportal.db", "r");
        if ($fd) {
                while (!feof($fd)) {
                        $line = trim(fgets($fd));
                        if ($line) {
                                $cpdb[] = explode(",", $line);
                        }
                }
                fclose($fd);
        }
        return $cpdb;
}

/* write captive portal DB */
function captiveportal_write_db($cpdb) {
                 
        global $g;
                
        $fd = @fopen("{$g['vardb_path']}/captiveportal.db", "w");
        if ($fd) { 
                foreach ($cpdb as $cpent) {
                        fwrite($fd, join(",", $cpent) . "\n");
                }       
                fclose($fd);
        }       
}

function captiveportal_write_elements() {
    global $g, $config;
    
    /* delete any existing elements */
    if (is_dir($g['captiveportal_element_path'])) {
        $dh = opendir($g['captiveportal_element_path']);
        while (($file = readdir($dh)) !== false) {
            if ($file != "." && $file != "..")
                unlink($g['captiveportal_element_path'] . "/" . $file);
        }
        closedir($dh);
    } else {
        mkdir($g['captiveportal_element_path']);
    }
    
    if (is_array($config['captiveportal']['element'])) {
    
        foreach ($config['captiveportal']['element'] as $data) {
            $fd = @fopen($g['captiveportal_element_path'] . '/' . $data['name'], "wb");
            if (!$fd) {
                printf("Error: cannot open '{$data['name']}' in captiveportal_write_elements().\n");
                return 1;
            }
            $decoded = base64_decode($data['content']);
            fwrite($fd,$decoded);
            fclose($fd);
        }
    }
    
    return 0;
}

/**
 * This function will calculate the lowest free firewall ruleno
 * within the range specified based on the actual installed rules
 *
 */

function captiveportal_get_next_ipfw_ruleno($rulenos_start = 10000, $rulenos_range_max = 9899) {

    exec("/sbin/ipfw show", $fwrules);
    foreach ($fwrules as $fwrule) {
        preg_match("/^(\d+)\s+/", $fwrule, $matches);
        $rulenos_used[] = $matches[1];
    }
    $rulenos_used = array_unique($rulenos_used);
    $rulenos_range = count($rulenos_used);
    if ($rulenos_range > $rulenos_range_max) {
        return NULL;
    }
    $rulenos_pool = range($rulenos_start, ($rulenos_start + $rulenos_range));
    $rulenos_free = array_diff($rulenos_pool, $rulenos_used);
    $ruleno = array_shift($rulenos_free);

    return $ruleno;
}


/**
 * This function will calculate the traffic produced by a client
 * based on its firewall rule
 *
 * Point of view: NAS
 *
 * Input means: from the client
 * Output means: to the client
 *
 */

function getVolume($ruleno) {

    $volume = array();

    // Initialize vars properly, since we don't want NULL vars
    $volume['input_pkts'] = $volume['input_bytes'] = $volume['output_pkts'] = $volume['output_bytes'] = 0 ;

    // Ingress
    exec("/sbin/ipfw show {$ruleno}", $ipfw);
    preg_match("/(\d+)\s+(\d+)\s+(\d+)\s+.*/", $ipfw[0], $matches);
    $volume['input_pkts'] = $matches[2];
    $volume['input_bytes'] = $matches[3];

    // Flush internal buffer
    unset($matches);

    // Outgress
    preg_match("/(\d+)\s+(\d+)\s+(\d+)\s+.*/", $ipfw[1], $matches);
    $volume['output_pkts'] = $matches[2];
    $volume['output_bytes'] = $matches[3];

    return $volume;
}

/**
 * Get the NAS-Identifier
 *
 * We will use our local hostname to make up the nas_id
 */

function getNasID()
{
    exec("/bin/hostname", $nasId);
    if(!$nasId[0])
        $nasId[0] = "m0n0wall";
    return $nasId[0];
}

/**
 * Get the NAS-IP-Address based on the current wan address
 *
 * Use functions in interfaces.inc to find this out
 *
 */

function getNasIP()
{
    $nasIp = get_current_wan_address();
    if(!$nasIp)
        $nasIp = "0.0.0.0";
    return $nasIp;
}

function portal_mac_fixed($clientmac) {
    global $g ;

    /* open captive portal mac db */
    if (file_exists("{$g['vardb_path']}/captiveportal_mac.db")) {
        $fd = @fopen("{$g['vardb_path']}/captiveportal_mac.db","r") ;
        if (!$fd) {
            return FALSE;
        }
        while (!feof($fd)) {
            $mac = trim(fgets($fd)) ;
            if(strcasecmp($clientmac, $mac) == 0) {
                fclose($fd) ;
                return TRUE ;
            }
        }
        fclose($fd) ;
    }
    return FALSE ;
}

?>
