#!/usr/local/bin/php
<?php 
/*
	$Id: status_graph_cpu.php 246 2008-03-02 21:23:20Z mkasper $
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

$pgtitle = array("Status", "CPU load");
require("guiconfig.inc");
?>
<?php include("fbegin.inc"); ?>
<div align="center">
<object data="graph_cpu.php" type="image/svg+xml" width="550" height="275">
<param name="src" value="graph_cpu.php" />
Your browser does not support the type SVG! You need to either use Firefox or download the Adobe SVG plugin.
</object>
</div>
<br><span class="red"><strong>Note:</strong></span> if you can't see the graph, you may need to download the most recent version of the <a href="http://www.mozilla.com/" target="_blank">Firefox</a> browser or install the <a href="http://www.adobe.com/svg/viewer/install/" target="_blank">Adobe SVG viewer</a>.
<?php include("fend.inc"); ?>
