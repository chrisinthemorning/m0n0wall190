#!/usr/local/bin/php
<?php 
/*
	$Id: diag_ipsec_spd.php 439 2011-04-27 16:17:42Z mkasper $
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
*/

$pgtitle = array("Diagnostics", "IPsec");

require("guiconfig.inc");
?>
<?php include("fbegin.inc"); ?>
<table width="100%" border="0" cellpadding="0" cellspacing="0">
  <tr><td class="tabnavtbl">
  <ul id="tabnav">
<?php 
    	$tabs = array('SAD' => 'diag_ipsec_sad.php',
            		  'SPD' => 'diag_ipsec_spd.php');
		dynamic_tab_menu($tabs);
?>
  </ul>
  </td></tr>
  <tr> 
    <td class="tabcont">
<?php

/* delete any SP? */
if ($_GET['act'] == "del") {
	$fd = @popen("/sbin/setkey -c > /dev/null 2>&1", "w");
	if ($fd) {
		fwrite($fd, "spddelete {$_GET['src']} {$_GET['dst']} any -P {$_GET['dir']} ;\n");
		pclose($fd);
		sleep(1);
	}
}

/* query SAD */
$fd = @popen("/sbin/setkey -DP", "r");
$spd = array();
if ($fd) {
	while (!feof($fd)) {
		$line = chop(fgets($fd));
		if (!$line)
			continue;
		if ($line == "No SPD entries.")
			break;
		if ($line[0] != "\t") {
			if (is_array($cursp))
				$spd[] = $cursp;
			$cursp = array();
			$linea = explode(" ", $line);
			$cursp['src'] = substr($linea[0], 0, strpos($linea[0], "["));
			$cursp['dst'] = substr($linea[1], 0, strpos($linea[1], "["));
			$i = 0;
		} else if (is_array($cursp)) {
			$linea = explode(" ", trim($line));
			if ($i == 1) {
				if ($linea[1] == "none")	/* don't show default anti-lockout rule */
					unset($cursp);
				else
					$cursp['dir'] = $linea[0];
			} else if ($i == 2) {
				$upperspec = explode("/", $linea[0]);
				$cursp['proto'] = $upperspec[0];
				list($cursp['ep_src'], $cursp['ep_dst']) = explode("-", $upperspec[2]);
			}
		}
		$i++;
	}
	if (is_array($cursp) && count($cursp))
		$spd[] = $cursp;
	pclose($fd);
}
if (count($spd)):
?>
            <table width="100%" border="0" cellpadding="0" cellspacing="0" summary="inner content pane">
  <tr>
                <td nowrap class="listhdrr">Source</td>
                <td nowrap class="listhdrr">Destination</td>
                <td nowrap class="listhdrr">Direction</td>
                <td nowrap class="listhdrr">Protocol</td>
                <td nowrap class="listhdrr">Tunnel endpoints</td>
                <td nowrap class="list"></td>
	</tr>
<?php
foreach ($spd as $sp): ?>
	<tr>
		<td class="listlr" valign="top"><?=htmlspecialchars($sp['src']);?></td>
		<td class="listr" valign="top"><?=htmlspecialchars($sp['dst']);?></td>
		<td class="listr" valign="top"><img src="<?=$sp['dir'];?>.gif" width="11" height="11" style="margin-top: 2px"></td>
		<td class="listr" valign="top"><?=htmlspecialchars(strtoupper($sp['proto']));?></td>
		<td class="listr" valign="top"><?=htmlspecialchars($sp['ep_src']);?> - <br>
			<?=htmlspecialchars($sp['ep_dst']);?></td>
		<td class="list" nowrap>
		<?php
			$args = "src=" . rawurlencode($sp['src']);
			$args .= "&amp;dst=" . rawurlencode($sp['dst']);
			$args .= "&amp;dir=" . rawurlencode($sp['dir']);
		?>
		  <a href="diag_ipsec_spd.php?act=del&amp;<?=$args;?>" onclick="return confirm('Do you really want to delete this security policy?')"><img src="x.gif" title="delete SP" width="17" height="17" border="0" alt="delete SP"></a>
		</td>
				
	</tr>
<?php endforeach; ?>
</table>
<br>
<table border="0" cellspacing="0" cellpadding="0" summary="inout content pane">
  <tr> 
	<td width="16"><img src="in.gif" width="11" height="11" alt=""></td>
	<td>incoming (as seen by firewall)</td>
  </tr>
  <tr> 
	<td colspan="5" height="4"></td>
  </tr>
  <tr> 
	<td><img src="out.gif" width="11" height="11" alt=""></td>
	<td>outgoing (as seen by firewall)</td>
  </tr>
</table>
<?php else: ?>
<p><strong>No IPsec security policies.</strong></p>
<?php endif; ?>
</td></tr></table>
<?php include("fend.inc"); ?>
