<?php
session_start();
#
global $title, $miner, $port, $readonly, $notify, $rigs;
global $rigipsecurity, $rigtotals, $forcerigtotals;
global $socksndtimeoutsec, $sockrcvtimeoutsec;
global $checklastshare, $poolinputs, $hidefields;
global $ignorerefresh, $changerefresh, $autorefresh;
global $allowcustompages, $customsummarypages;
global $miner_font_family, $miner_font_size;
global $colouroverride, $placebuttons;
#
# See API-README for more details of these variables and how
# to configure miner.php
#
# Web page title
$title = 'Mine';
#
# Set $readonly to true to force miner.php to be readonly
# Set $readonly to false then it will check cgminer 'privileged'
$readonly = false;
#
# Set $notify to false to NOT attempt to display the notify command
# Set $notify to true to attempt to display the notify command
$notify = true;
#
# Set $checklastshare to true to do the following checks:
# If a device's last share is 12x expected ago then display as an error
# If a device's last share is 8x expected ago then display as a warning
# If either of the above is true, also display the whole line highlighted
# This assumes shares are 1 difficulty shares
$checklastshare = true;
#
# Set $poolinputs to true to show the input fields for adding a pool
# and changing the pool priorities
# N.B. also if $readonly is true, it will not display the fields
$poolinputs = false;
#
# Set $rigs to an array of your cgminer rigs that are running
#  format: 'IP:Port' or 'Host:Port' or 'Host:Port:Name'
$rigs = array('127.0.0.1:4028');
#
# Set $rigipsecurity to false to show the IP/Port of the rig
# in the socket error messages and also show the full socket message
$rigipsecurity = true;
#
# Set $rigtotals to true to display totals on the single rig page
# 'false' means no totals (and ignores $forcerigtotals)
# You can force it to always show rig totals when there is only
# one line by setting $forcerigtotals = true;
$rigtotals = true;
$forcerigtotals = false;
#
# These should be OK for most cases
$socksndtimeoutsec = 10;
$sockrcvtimeoutsec = 40;
#
# List of fields NOT to be displayed
# This example would hide the slightly more sensitive pool information
#$hidefields = array('POOL.URL' => 1, 'POOL.User' => 1);
$hidefields = array();
#
# Auto-refresh of the page (in seconds) - integers only
# $ignorerefresh = true/false always ignore refresh parameters
# $changerefresh = true/false show buttons to change the value
# $autorefresh = default value, 0 means dont auto-refresh
$ignorerefresh = false;
$changerefresh = true;
$autorefresh = 0;
#
# Should we allow custom pages?
# (or just completely ignore them and don't display the buttons)
$allowcustompages = true;
#
# OK this is a bit more complex item: Custom Summary Pages
# As mentioned above, see API-README
# see the example below (if there is no matching data, no total will show)
$mobilepage = array(
 'DATE' => null,
 'RIGS' => null,
 'SUMMARY' => array('Elapsed', 'MHS av', 'Found Blocks=Blks', 'Accepted', 'Rejected=Rej', 'Utility'),
 'DEVS+NOTIFY' => array('DEVS.Name=Name', 'DEVS.ID=ID', 'DEVS.Status=Status', 'DEVS.Temperature=Temp',
			'DEVS.MHS av=MHS av', 'DEVS.Accepted=Accept', 'DEVS.Rejected=Rej',
			'DEVS.Utility=Utility', 'NOTIFY.Last Not Well=Not Well'),
 'POOL' => array('POOL', 'Status', 'Accepted', 'Rejected=Rej', 'Last Share Time'));
$mobilesum = array(
 'SUMMARY' => array('MHS av', 'Found Blocks', 'Accepted', 'Rejected', 'Utility'),
 'DEVS+NOTIFY' => array('DEVS.MHS av', 'DEVS.Accepted', 'DEVS.Rejected', 'DEVS.Utility'),
 'POOL' => array('Accepted', 'Rejected'));
#
$statspage = array(
 'DATE' => null,
 'RIGS' => null,
 'SUMMARY' => array('Elapsed', 'MHS av', 'Found Blocks=Blks',
			'Accepted', 'Rejected=Rej', 'Utility',
			'Hardware Errors=HW Errs', 'Network Blocks=Net Blks',
			'Work Utility'),
 'COIN' => array('*'),
 'STATS' => array('*'));
#
$statssum = array(
 'SUMMARY' => array('MHS av', 'Found Blocks', 'Accepted',
			'Rejected', 'Utility', 'Hardware Errors',
			'Work Utility'));
#
# customsummarypages is an array of these Custom Summary Pages
$customsummarypages = array('Mobile' => array($mobilepage, $mobilesum),
 'Stats' => array($statspage, $statssum));
#
$here = $_SERVER['PHP_SELF'];
#
global $tablebegin, $tableend, $warnfont, $warnoff, $dfmt;
#
$tablebegin = '<tr><td><table border=1 cellpadding=5 cellspacing=0>';
$tableend = '</table></td></tr>';
$warnfont = '<font color=red><b>';
$warnoff = '</b></font>';
$dfmt = 'H:i:s j-M-Y \U\T\CP';
#
$miner_font_family = 'verdana,arial,sans';
$miner_font_size = '13pt';
#
# Edit this or redefine it in myminer.php to change the colour scheme
# See $colourtable below for the list of names
$colouroverride = array();
#
# Where to place the buttons: 'top' 'bot' 'both'
#  anything else means don't show them - case sensitive
$placebuttons = 'top';
#
# This below allows you to put your own settings into a seperate file
# so you don't need to update miner.php with your preferred settings
# every time a new version is released
# Just create the file 'myminer.php' in the same directory as
# 'miner.php' - and put your own settings in there
if (file_exists('myminer.php'))
 include_once('myminer.php');
#
# This is the system default that must always contain all necessary
# colours so it must be a constant
# You can override these values with $colouroverride
# The only one missing is in $warnfont
# - which you can override directly anyway
global $colourtable;
$colourtable = array(
	'body bgcolor'		=> '#ecffff',
	'td color'		=> 'blue',
	'td.two color'		=> 'blue',
	'td.two background'	=> '#ecffff',
	'td.h color'		=> 'blue',
	'td.h background'	=> '#c4ffff',
	'td.err color'		=> 'black',
	'td.err background'	=> '#ff3050',
	'td.warn color'		=> 'black',
	'td.warn background'	=> '#ffb050',
	'td.sta color'		=> 'green',
	'td.tot color'		=> 'blue',
	'td.tot background'	=> '#fff8f2',
	'td.lst color'		=> 'blue',
	'td.lst background'	=> '#ffffdd',
	'td.hi color'		=> 'blue',
	'td.hi background'	=> '#f6ffff',
	'td.lo color'		=> 'blue',
	'td.lo background'	=> '#deffff'
);
#
# Don't touch these 2
$miner = null;
$port = null;
#
# Ensure it is only ever shown once
global $showndate;
$showndate = false;
#
# For summary page to stop retrying failed rigs
global $rigerror;
$rigerror = array();
#
global $rownum;
$rownum = 0;
#
function getcss($cssname, $dom = false)
{
 global $colourtable, $colouroverride;

 $css = '';
 foreach ($colourtable as $cssdata => $value)
 {
	$cssobj = explode(' ', $cssdata, 2);
	if ($cssobj[0] == $cssname)
	{
		if (isset($colouroverride[$cssdata]))
			$value = $colouroverride[$cssdata];

		if ($dom == true)
			$css .= ' '.$cssobj[1].'='.$value;
		else
			$css .= $cssobj[1].':'.$value.'; ';
	}
 }
 return $css;
}
#
function getdom($domname)
{
 return getcss($domname, true);
}
#
function htmlhead($checkapi, $rig, $pg = null)
{
 global $title, $miner_font_family, $miner_font_size;
 global $error, $readonly, $poolinputs, $here;
 global $ignorerefresh, $autorefresh;

 $extraparams = '';
 if ($rig != null && $rig != '')
	$extraparams = "&rig=$rig";
 else
	if ($pg != null && $pg != '')
		$extraparams = "&pg=$pg";

 if ($ignorerefresh == true || $autorefresh == 0)
	$refreshmeta = '';
 else
 {
	$url = "$here?ref=$autorefresh$extraparams";
	$refreshmeta = "\n<meta http-equiv='refresh' content='$autorefresh;url=$url'>";
 }

 if ($readonly === false && $checkapi === true)
 {
	$error = null;
	$access = api($rig, 'privileged');
	if ($error != null
	||  !isset($access['STATUS']['STATUS'])
	||  $access['STATUS']['STATUS'] != 'S')
		$readonly = true;
 }
 $miner_font = "font-family:$miner_font_family; font-size:$miner_font_size;";

 echo "<html><head>$refreshmeta
<title>$title</title>
<style type='text/css'>
td { $miner_font ".getcss('td')."}
td.two { $miner_font ".getcss('td.two')."}
td.h { $miner_font ".getcss('td.h')."}
td.err { $miner_font ".getcss('td.err')."}
td.warn { $miner_font ".getcss('td.warn')."}
td.sta { $miner_font ".getcss('td.sta')."}
td.tot { $miner_font ".getcss('td.tot')."}
td.lst { $miner_font ".getcss('td.lst')."}
td.hi { $miner_font ".getcss('td.hi')."}
td.lo { $miner_font ".getcss('td.lo')."}
</style>
</head><body".getdom('body').">
<script type='text/javascript'>
function pr(a,m){if(m!=null){if(!confirm(m+'?'))return}window.location='$here?ref=$autorefresh'+a}\n";

if ($ignorerefresh == false)
 echo "function prr(a){if(a){v=document.getElementById('refval').value}else{v=0}window.location='$here?ref='+v+'$extraparams'}\n";

 if ($readonly === false && $checkapi === true)
 {
echo "function prc(a,m){pr('&arg='+a,m)}
function prs(a,r){var c=a.substr(3);var z=c.split('|',2);var m=z[0].substr(0,1).toUpperCase()+z[0].substr(1)+' GPU '+z[1];prc(a+'&rig='+r,m)}
function prs2(a,n,r){var v=document.getElementById('gi'+n).value;var c=a.substr(3);var z=c.split('|',2);var m='Set GPU '+z[1]+' '+z[0].substr(0,1).toUpperCase()+z[0].substr(1)+' to '+v;prc(a+','+v+'&rig='+r,m)}\n";
	if ($poolinputs === true)
		echo "function cbs(s){var t=s.replace(/\\\\/g,'\\\\\\\\'); return t.replace(/,/g, '\\\\,')}\nfunction pla(r){var u=document.getElementById('purl').value;var w=document.getElementById('pwork').value;var p=document.getElementById('ppass').value;pr('&rig='+r+'&arg=addpool|'+cbs(u)+','+cbs(w)+','+cbs(p),'Add Pool '+u)}\nfunction psp(r){var p=document.getElementById('prio').value;pr('&rig='+r+'&arg=poolpriority|'+p,'Set Pool Priorities to '+p)}\n";
 }
?>
</script>
<table width=100% height=100% border=0 cellpadding=0 cellspacing=0 summary='Mine'>
<tr><td align=center valign=top>
<table border=0 cellpadding=4 cellspacing=0 summary='Mine'>
<?php
}
#
global $haderror, $error;
$haderror = false;
$error = null;
#
function getsock($rig, $addr, $port)
{
 global $rigipsecurity;
 global $haderror, $error, $socksndtimeoutsec, $sockrcvtimeoutsec;

 $error = null;
 $socket = null;
 $socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
 if ($socket === false || $socket === null)
 {
	$haderror = true;
	if ($rigipsecurity === false)
	{
		$error = socket_strerror(socket_last_error());
		$msg = "socket create(TCP) failed";
		$error = "ERR: $msg '$error'\n";
	}
	else
		$error = "ERR: socket create(TCP) failed\n";

	return null;
 }

 // Ignore if this fails since the socket connect may work anyway
 //  and nothing is gained by aborting if the option cannot be set
 //  since we don't know in advance if it can connect
 socket_set_option($socket, SOL_SOCKET, SO_SNDTIMEO, array('sec' => $socksndtimeoutsec, 'usec' => 0));
 socket_set_option($socket, SOL_SOCKET, SO_RCVTIMEO, array('sec' => $sockrcvtimeoutsec, 'usec' => 0));

 $res = socket_connect($socket, $addr, $port);
 if ($res === false)
 {
	$haderror = true;
	if ($rigipsecurity === false)
	{
		$error = socket_strerror(socket_last_error());
		$msg = "socket connect($addr,$port) failed";
		$error = "ERR: $msg '$error'\n";
	}
	else
		$error = "ERR: socket connect($rig) failed\n";

	socket_close($socket);
	return null;
 }
 return $socket;
}
#
function readsockline($socket)
{
 $line = '';
 while (true)
 {
	$byte = socket_read($socket, 1);
	if ($byte === false || $byte === '')
		break;
	if ($byte === "\0")
		break;
	$line .= $byte;
 }
 return $line;
}
#
function api_convert_escape($str)
{
 $res = '';
 $len = strlen($str);
 for ($i = 0; $i < $len; $i++)
 {
	$ch = substr($str, $i, 1);
	if ($ch != '\\' || $i == ($len-1))
		$res .= $ch;
	else
	{
		$i++;
		$ch = substr($str, $i, 1);
		switch ($ch)
		{
		case '|':
			$res .= "\1";
			break;
		case '\\':
			$res .= "\2";
			break;
		case '=':
			$res .= "\3";
			break;
		case ',':
			$res .= "\4";
			break;
		default:
			$res .= $ch;
		}
	}
 }
 return $res;
}
#
function revert($str)
{
 return str_replace(array("\1", "\2", "\3", "\4"), array("|", "\\", "=", ","), $str);
}
#
function api($rig, $cmd)
{
 global $haderror, $error;
 global $miner, $port, $hidefields;

 $socket = getsock($rig, $miner, $port);
 if ($socket != null)
 {
	socket_write($socket, $cmd, strlen($cmd));
	$line = readsockline($socket);
	socket_close($socket);

	if (strlen($line) == 0)
	{
		$haderror = true;
		$error = "WARN: '$cmd' returned nothing\n";
		return $line;
	}

#	print "$cmd returned '$line'\n";

	$line = api_convert_escape($line);

	$data = array();

	$objs = explode('|', $line);
	foreach ($objs as $obj)
	{
		if (strlen($obj) > 0)
		{
			$items = explode(',', $obj);
			$item = $items[0];
			$id = explode('=', $items[0], 2);
			if (count($id) == 1 or !ctype_digit($id[1]))
				$name = $id[0];
			else
				$name = $id[0].$id[1];

			if (strlen($name) == 0)
				$name = 'null';

			$sectionname = preg_replace('/\d/', '', $name);

			if (isset($data[$name]))
			{
				$num = 1;
				while (isset($data[$name.$num]))
					$num++;
				$name .= $num;
			}

			$counter = 0;
			foreach ($items as $item)
			{
				$id = explode('=', $item, 2);

				if (isset($hidefields[$sectionname.'.'.$id[0]]))
					continue;

				if (count($id) == 2)
					$data[$name][$id[0]] = revert($id[1]);
				else
					$data[$name][$counter] = $id[0];

				$counter++;
			}
		}
	}
	return $data;
 }
 return null;
}
#
function getparam($name, $both = false)
{
 $a = null;
 if (isset($_POST[$name]))
	$a = $_POST[$name];

 if (($both === true) and ($a === null))
 {
	if (isset($_GET[$name]))
		$a = $_GET[$name];
 }

 if ($a == '' || $a == null)
	return null;

 // limit to 1K just to be safe
 return substr($a, 0, 1024);
}
#
function newtable()
{
 global $tablebegin, $rownum;
 echo $tablebegin;
 $rownum = 0;
}
#
function newrow()
{
 echo '<tr>';
}
#
function otherrow($row)
{
 echo "<tr>$row</tr>";
}
#
function endrow()
{
 global $rownum;
 echo '</tr>';
 $rownum++;
}
#
function endtable()
{
 global $tableend;
 echo $tableend;
}
#
function classlastshare($when, $alldata, $warnclass, $errorclass)
{
 global $checklastshare;

 if ($checklastshare === false)
	return '';

 if ($when == 0)
	return '';

 if (!isset($alldata['MHS av']))
	return '';

 if ($alldata['MHS av'] == 0)
	return '';

 if (!isset($alldata['Last Share Time']))
	return '';

 if (!isset($alldata['Last Share Difficulty']))
	return '';

 $expected = pow(2, 32) / ($alldata['MHS av'] * pow(10, 6));

 // If the share difficulty changes while waiting on a share,
 // this calculation will of course be incorrect
 $expected *= $alldata['Last Share Difficulty'];

 $howlong = $when - $alldata['Last Share Time'];
 if ($howlong < 1)
	$howlong = 1;

 if ($howlong > ($expected * 12))
	return $errorclass;

 if ($howlong > ($expected * 8))
	return $warnclass;

 return '';
}
#
function fmt($section, $name, $value, $when, $alldata)
{
 global $dfmt, $rownum;

 if ($alldata == null)
	$alldata = array();

 $errorclass = ' class=err';
 $warnclass = ' class=warn';
 $lstclass = ' class=lst';
 $hiclass = ' class=hi';
 $loclass = ' class=lo';
 $c2class = ' class=two';
 $totclass = ' class=tot';
 $b = '&nbsp;';

 $ret = $value;
 $class = '';

 $nams = explode('.', $name);
 if (count($nams) > 1)
	$name = $nams[count($nams)-1];

 if ($value === null)
	$ret = $b;
 else
	switch ($section.'.'.$name)
	{
	case 'GPU.Last Share Time':
	case 'PGA.Last Share Time':
	case 'DEVS.Last Share Time':
		if ($value == 0
		||  (isset($alldata['Last Share Pool']) && $alldata['Last Share Pool'] == -1))
		{
			$ret = 'Never';
			$class = $warnclass;
		}
		else
		{
			$ret = date('H:i:s', $value);
			$class = classlastshare($when, $alldata, $warnclass, $errorclass);
		}
		break;
	case 'POOL.Last Share Time':
		if ($value == 0)
			$ret = 'Never';
		else
			$ret = date('H:i:s d-M', $value);
		break;
	case 'GPU.Last Share Pool':
	case 'PGA.Last Share Pool':
	case 'DEVS.Last Share Pool':
		if ($value == -1)
		{
			$ret = 'None';
			$class = $warnclass;
		}
		break;
	case 'SUMMARY.Elapsed':
		$s = $value % 60;
		$value -= $s;
		$value /= 60;
		if ($value == 0)
			$ret = $s.'s';
		else
		{
			$m = $value % 60;
			$value -= $m;
			$value /= 60;
			if ($value == 0)
				$ret = sprintf("%dm$b%02ds", $m, $s);
			else
			{
				$h = $value % 24;
				$value -= $h;
				$value /= 24;
				if ($value == 0)
					$ret = sprintf("%dh$b%02dm$b%02ds", $h, $m, $s);
				else
				{
					if ($value == 1)
						$days = '';
					else
						$days = 's';
	
					$ret = sprintf("%dday$days$b%02dh$b%02dm$b%02ds", $value, $h, $m, $s);
				}
			}
		}
		break;
	case 'NOTIFY.Last Well':
		if ($value == '0')
		{
			$ret = 'Never';
			$class = $warnclass;
		}
		else
			$ret = date('H:i:s', $value);
		break;
	case 'NOTIFY.Last Not Well':
		if ($value == '0')
			$ret = 'Never';
		else
		{
			$ret = date('H:i:s', $value);
			$class = $errorclass;
		}
		break;
	case 'NOTIFY.Reason Not Well':
		if ($value != 'None')
			$class = $errorclass;
		break;
	case 'GPU.Utility':
	case 'PGA.Utility':
	case 'DEVS.Utility':
	case 'SUMMARY.Utility':
	case 'total.Utility':
		$ret = $value.'/m';
		if ($value == 0)
			$class = $errorclass;
		else
			if (isset($alldata['Difficulty Accepted'])
			&&  isset($alldata['Accepted'])
			&&  isset($alldata['MHS av'])
			&&  ($alldata['Difficulty Accepted'] > 0)
			&&  ($alldata['Accepted'] > 0))
			{
				$expected = 60 * $alldata['MHS av'] * (pow(10, 6) / pow(2, 32));
				if ($expected == 0)
					$expected = 0.000001; // 1 H/s

				$da = $alldata['Difficulty Accepted'];
				$a = $alldata['Accepted'];
				$expected /= ($da / $a);

				$ratio = $value / $expected;
				if ($ratio < 0.9)
					$class = $loclass;
				else
					if ($ratio > 1.1)
						$class = $hiclass;
			}
		break;
	case 'SUMMARY.Work Utility':
	case 'total.Work Utility':
		$ret = $value.'/m';
		break;
	case 'PGA.Temperature':
	case 'GPU.Temperature':
	case 'DEVS.Temperature':
		$ret = $value.'&deg;C';
		if (!isset($alldata['GPU']))
			break;
	case 'GPU.GPU Clock':
	case 'DEVS.GPU Clock':
	case 'GPU.Memory Clock':
	case 'DEVS.Memory Clock':
	case 'GPU.GPU Voltage':
	case 'DEVS.GPU Voltage':
	case 'GPU.GPU Activity':
	case 'DEVS.GPU Activity':
		if ($value == 0)
			$class = $warnclass;
		break;
	case 'GPU.Fan Percent':
	case 'DEVS.Fan Percent':
		if ($value == 0)
			$class = $warnclass;
		else
		{
			if ($value == 100)
				$class = $errorclass;
			else
				if ($value > 85)
					$class = $warnclass;
		}
		break;
	case 'GPU.Fan Speed':
	case 'DEVS.Fan Speed':
		if ($value == 0)
			$class = $warnclass;
		else
			if (isset($alldata['Fan Percent']))
			{
				$test = $alldata['Fan Percent'];
				if ($test == 100)
					$class = $errorclass;
				else
					if ($test > 85)
						$class = $warnclass;
			}
		break;
	case 'GPU.MHS av':
	case 'PGA.MHS av':
	case 'DEVS.MHS av':
	case 'SUMMARY.MHS av':
	case 'total.MHS av':
		$parts = explode('.', $value, 2);
		if (count($parts) == 1)
			$dec = '';
		else
			$dec = '.'.$parts[1];
		$ret = number_format((float)$parts[0]).$dec;

		if ($value == 0)
			$class = $errorclass;
		else
			if (isset($alldata['Difficulty Accepted'])
			&&  isset($alldata['Accepted'])
			&&  isset($alldata['Utility'])
			&&  ($alldata['Difficulty Accepted'] > 0)
			&&  ($alldata['Accepted'] > 0))
			{
				$expected = 60 * $value * (pow(10, 6) / pow(2, 32));
				if ($expected == 0)
					$expected = 0.000001; // 1 H/s

				$da = $alldata['Difficulty Accepted'];
				$a = $alldata['Accepted'];
				$expected /= ($da / $a);

				$ratio = $alldata['Utility'] / $expected;
				if ($ratio < 0.9)
					$class = $hiclass;
				else
					if ($ratio > 1.1)
						$class = $loclass;
			}
		break;
	case 'GPU.Total MH':
	case 'PGA.Total MH':
	case 'DEVS.Total MH':
	case 'SUMMARY.Total MH':
	case 'total.Total MH':
	case 'SUMMARY.Getworks':
	case 'POOL.Getworks':
	case 'total.Getworks':
	case 'GPU.Accepted':
	case 'PGA.Accepted':
	case 'DEVS.Accepted':
	case 'SUMMARY.Accepted':
	case 'POOL.Accepted':
	case 'total.Accepted':
	case 'GPU.Rejected':
	case 'PGA.Rejected':
	case 'DEVS.Rejected':
	case 'SUMMARY.Rejected':
	case 'POOL.Rejected':
	case 'total.Rejected':
	case 'SUMMARY.Local Work':
	case 'total.Local Work':
	case 'SUMMARY.Discarded':
	case 'POOL.Discarded':
	case 'total.Discarded':
	case 'POOL.Diff1 Shares':
	case 'total.Diff1 Shares':
	case 'GPU.Diff1 Work':
	case 'PGA.Diff1 Work':
	case 'total.Diff1 Work':
		$parts = explode('.', $value, 2);
		if (count($parts) == 1)
			$dec = '';
		else
			$dec = '.'.$parts[1];
		$ret = number_format((float)$parts[0]).$dec;
		break;
	case 'GPU.Status':
	case 'PGA.Status':
	case 'DEVS.Status':
	case 'POOL.Status':
		if ($value != 'Alive')
			$class = $errorclass;
		break;
	case 'GPU.Enabled':
	case 'PGA.Enabled':
	case 'DEVS.Enabled':
		if ($value != 'Y')
			$class = $warnclass;
		break;
	case 'STATUS.When':
	case 'COIN.Current Block Time':
		$ret = date($dfmt, $value);
		break;
	case 'BUTTON.Rig':
	case 'BUTTON.Pool':
	case 'BUTTON.GPU':
		$ret = $value;
		break;
	}

 if ($section == 'NOTIFY' && substr($name, 0, 1) == '*' && $value != '0')
	$class = $errorclass;

 if ($class == '' && $section != 'POOL')
	$class = classlastshare($when, $alldata, $lstclass, $lstclass);

 if ($class == '' && $section == 'total')
	$class = $totclass;

 if ($class == '' && ($rownum % 2) == 0)
	$class = $c2class;

 if ($ret === '')
	$ret = $b;

 return array($ret, $class);
}
#
global $poolcmd;
$poolcmd = array(	'Switch to'	=> 'switchpool',
			'Enable'	=> 'enablepool',
			'Disable'	=> 'disablepool',
			'Remove'	=> 'removepool' );
#
function showhead($cmd, $values, $justnames = false)
{
 global $poolcmd, $readonly;

 newrow();

 foreach ($values as $name => $value)
 {
	if ($name == '0' or $name == '')
		$name = '&nbsp;';
	echo "<td valign=bottom class=h>$name</td>";
 }

 if ($justnames === false && $cmd == 'pools' && $readonly === false)
	foreach ($poolcmd as $name => $pcmd)
		echo "<td valign=bottom class=h>$name</td>";

 endrow();
}
#
function showdatetime()
{
 global $dfmt;

 otherrow('<td class=sta>Date: '.date($dfmt).'</td>');
}
#
global $singlerigsum;
$singlerigsum = array(
 'devs' => array('MHS av' => 1, 'MHS 5s' => 1, 'Accepted' => 1, 'Rejected' => 1,
			'Hardware Errors' => 1, 'Utility' => 1, 'Total MH' => 1),
 'pools' => array('Getworks' => 1, 'Accepted' => 1, 'Rejected' => 1, 'Discarded' => 1,
			'Stale' => 1, 'Get Failures' => 1, 'Remote Failures' => 1,
			'Diff1 Shares' => 1),
 'notify' => array('*' => 1));
#
function showtotal($total, $when, $oldvalues)
{
 global $rigtotals;

 list($showvalue, $class) = fmt('total', '', 'Total:', $when, null);
 echo "<td$class align=right>$showvalue</td>";

 $skipfirst = true;
 foreach ($oldvalues as $name => $value)
 {
	if ($skipfirst === true)
	{
		$skipfirst = false;
		continue;
	}

	if (isset($total[$name]))
		$newvalue = $total[$name];
	else
		$newvalue = '';

	list($showvalue, $class) = fmt('total', $name, $newvalue, $when, null);
	echo "<td$class";
	if ($rigtotals === true)
		echo ' align=right';
	echo ">$showvalue</td>";
 }
}
#
function details($cmd, $list, $rig)
{
 global $dfmt, $poolcmd, $readonly, $showndate;
 global $rownum, $rigtotals, $forcerigtotals, $singlerigsum;

 $when = 0;

 $stas = array('S' => 'Success', 'W' => 'Warning', 'I' => 'Informational', 'E' => 'Error', 'F' => 'Fatal');

 newtable();

 if ($showndate === false)
 {
	showdatetime();

	endtable();
	newtable();

	$showndate = true;
 }

 if (isset($list['STATUS']))
 {
	newrow();
	echo '<td>Computer: '.$list['STATUS']['Description'].'</td>';
	if (isset($list['STATUS']['When']))
	{
		echo '<td>When: '.date($dfmt, $list['STATUS']['When']).'</td>';
		$when = $list['STATUS']['When'];
	}
	$sta = $list['STATUS']['STATUS'];
	echo '<td>Status: '.$stas[$sta].'</td>';
	echo '<td>Message: '.$list['STATUS']['Msg'].'</td>';
	endrow();
 }

 if ($rigtotals === true && isset($singlerigsum[$cmd]))
	$dototal = $singlerigsum[$cmd];
 else
	$dototal = array();

 $total = array();

 $section = '';
 $oldvalues = null;
 foreach ($list as $item => $values)
 {
	if ($item == 'STATUS')
		continue;

	$sectionname = preg_replace('/\d/', '', $item);

	// Handle 'devs' possibly containing >1 table
	if ($sectionname != $section)
	{
		if ($oldvalues != null && count($total) > 0
		&&  ($rownum > 2 || $forcerigtotals === true))
			showtotal($total, $when, $oldvalues);

		endtable();
		newtable();
		showhead($cmd, $values);
		$section = $sectionname;
	}

	newrow();

	foreach ($values as $name => $value)
	{
		list($showvalue, $class) = fmt($section, $name, $value, $when, $values);
		echo "<td$class";
		if ($rigtotals === true)
			echo ' align=right';
		echo ">$showvalue</td>";

		if (isset($dototal[$name])
		||  (isset($dototal['*']) and substr($name, 0, 1) == '*'))
		{
			if (isset($total[$name]))
				$total[$name] += $value;
			else
				$total[$name] = $value;
		}
	}

	if ($cmd == 'pools' && $readonly === false)
	{
		reset($values);
		$pool = current($values);
		foreach ($poolcmd as $name => $pcmd)
		{
			list($ignore, $class) = fmt('BUTTON', 'Pool', '', $when, $values);
			echo "<td$class>";
			if ($pool === false)
				echo '&nbsp;';
			else
			{
				echo "<input type=button value='Pool $pool'";
				echo " onclick='prc(\"$pcmd|$pool&rig=$rig\",\"$name Pool $pool\")'>";
			}
			echo '</td>';
		}
	}
	endrow();

	$oldvalues = $values;
 }

 if ($oldvalues != null && count($total) > 0
 &&  ($rownum > 2 || $forcerigtotals === true))
	showtotal($total, $when, $oldvalues);

 endtable();
}
#
global $devs;
$devs = null;
#
function gpubuttons($count, $rig)
{
 global $devs;

 $basic = array( 'GPU', 'Enable', 'Disable', 'Restart' );

 $options = array(	'intensity' => 'Intensity',
			'fan' => 'Fan Percent',
			'engine' => 'GPU Clock',
			'mem' => 'Memory Clock',
			'vddc' => 'GPU Voltage' );

 newtable();
 newrow();

 foreach ($basic as $head)
	echo "<td class=h>$head</td>";

 foreach ($options as $name => $des)
	echo "<td class=h nowrap>$des</td>";

 $n = 0;
 for ($c = 0; $c < $count; $c++)
 {
	endrow();
	newrow();

	foreach ($basic as $name)
	{
		list($ignore, $class) = fmt('BUTTON', 'GPU', '', 0, null);
		echo "<td$class>";

		if ($name == 'GPU')
			echo $c;
		else
		{
			echo "<input type=button value='$name $c' onclick='prs(\"gpu";
			echo strtolower($name);
			echo "|$c\",$rig)'>";
		}

		echo '</td>';
	}

	foreach ($options as $name => $des)
	{
		list($ignore, $class) = fmt('BUTTON', 'GPU', '', 0, null);
		echo "<td$class>";

		if (!isset($devs["GPU$c"][$des]))
			echo '&nbsp;';
		else
		{
			$value = $devs["GPU$c"][$des];
			echo "<input type=button value='Set $c:' onclick='prs2(\"gpu$name|$c\",$n,$rig)'>";
			echo "<input size=7 type=text name=gi$n value='$value' id=gi$n>";
			$n++;
		}

		echo '</td>';
	}

 }
 endrow();
 endtable();
}
#
function processgpus($rig)
{
 global $error;
 global $warnfont, $warnoff;

 $gpus = api($rig, 'gpucount');

 if ($error != null)
	otherrow("<td>Error getting GPU count: $warnfont$error$warnoff</td>");
 else
 {
	if (!isset($gpus['GPUS']['Count']))
	{
		$rw = '<td>No GPU count returned: '.$warnfont;
		$rw .= $gpus['STATUS']['STATUS'].' '.$gpus['STATUS']['Msg'];
		$rw .= $warnoff.'</td>';
		otherrow($rw);
	}
	else
	{
		$count = $gpus['GPUS']['Count'];
		if ($count == 0)
			otherrow('<td>No GPUs</td>');
		else
			gpubuttons($count, $rig);
	}
 }
}
#
function showpoolinputs($rig, $ans)
{
 global $readonly, $poolinputs;

 if ($readonly === true || $poolinputs === false)
	return;

 newtable();
 newrow();

 $inps = array('Pool URL' => array('purl', 20),
		'Worker Name' => array('pwork', 10),
		'Worker Password' => array('ppass', 10));
 $b = '&nbsp;';

 echo "<td align=right class=h> Add a pool: </td><td>";

 foreach ($inps as $text => $name)
	echo "$text: <input name='".$name[0]."' id='".$name[0]."' value='' type=text size=".$name[1]."> ";

 echo "</td><td align=middle><input type=button value='Add' onclick='pla($rig)'></td>";

 endrow();

 if (count($ans) > 1)
 {
	newrow();

	echo '<td align=right class=h> Set pool priorities: </td>';
	echo "<td> Comma list of pool numbers: <input type=text name=prio id=prio size=20>";
	echo "</td><td align=middle><input type=button value='Set' onclick='psp($rig)'></td>";

	endrow();
 }
 endtable();
}
#
function process($cmds, $rig)
{
 global $error, $devs;
 global $warnfont, $warnoff;

 $count = count($cmds);
 foreach ($cmds as $cmd => $des)
 {
	$process = api($rig, $cmd);

	if ($error != null)
	{
		otherrow("<td colspan=100>Error getting $des: $warnfont$error$warnoff</td>");
		break;
	}
	else
	{
		details($cmd, $process, $rig);

		if ($cmd == 'devs')
			$devs = $process;

		if ($cmd == 'pools')
			showpoolinputs($rig, $process);

		# Not after the last one
		if (--$count > 0)
			otherrow('<td><br><br></td>');
	}
 }
}
#
function rigbutton($rig, $rigname, $when, $row)
{
 global $rigs;

 if (isset($rigs[$rig]))
 {
	$parts = explode(':', $rigs[$rig], 3);
	if (count($parts) == 3)
		$rigname = $parts[2];
 }

 list($value, $class) = fmt('BUTTON', 'Rig', '', $when, $row);

 $button = "<td align=middle$class><input type=button value='$rigname'";
 $button .= " onclick='pr(\"&rig=$rig\",null)'></td>";

 return $button;
}
#
function showrigs($anss, $headname, $rigname)
{
 $dthead = array($headname => 1, 'STATUS' => 1, 'Description' => 1, 'When' => 1, 'API' => 1, 'CGMiner' => 1);
 showhead('', $dthead);

 foreach ($anss as $rig => $ans)
 {
	if ($ans == null)
		continue;

	newrow();

	$when = 0;
	if (isset($ans['STATUS']['When']))
		$when = $ans['STATUS']['When'];

	foreach ($ans as $item => $row)
	{
		if ($item != 'STATUS' && $item != 'VERSION')
			continue;

		foreach ($dthead as $name => $x)
		{
			if ($item == 'STATUS' && $name == $headname)
				echo rigbutton($rig, $rigname.$rig, $when, null);
			else
			{
				if (isset($row[$name]))
				{
					list($showvalue, $class) = fmt('STATUS', $name, $row[$name], $when, null);
					echo "<td$class align=right>$showvalue</td>";
				}
			}
		}
	}
	endrow();
 }
}
#
# $head is a hack but this is just a demo anyway :)
function doforeach($cmd, $des, $sum, $head, $datetime)
{
 global $miner, $port;
 global $error, $readonly, $notify, $rigs;
 global $warnfont, $warnoff, $dfmt;
 global $rigerror;

 $when = 0;

 $header = $head;
 $anss = array();

 $count = 0;
 $preverr = count($rigerror);
 foreach ($rigs as $num => $rig)
 {
	$anss[$num] = null;

	if (isset($rigerror[$rig]))
		continue;

	$parts = explode(':', $rig, 3);
	if (count($parts) >= 2)
	{
		$miner = $parts[0];
		$port = $parts[1];

		if (count($parts) > 2)
			$name = $parts[2];
		else
			$name = $num;

		$ans = api($name, $cmd);

		if ($error != null)
		{
			$rw = "<td colspan=100>Error on rig $name getting ";
			$rw .= "$des: $warnfont$error$warnoff</td>";
			otherrow($rw);
			$rigerror[$rig] = $error;
			$error = null;
		}
		else
		{
			$anss[$num] = $ans;
			$count++;
		}
	}
 }

 if ($count == 0)
 {
	$rw = '<td>Failed to access any rigs successfully';
	if ($preverr > 0)
		$rw .= ' (or rigs had previous errors)';
	$rw .= '</td>';
	otherrow($rw);
	return;
 }

 if ($datetime)
 {
	showdatetime();
	endtable();
	newtable();
	showrigs($anss, '', 'Rig ');
	endtable();
	otherrow('<td><br><br></td>');
	newtable();

	return;
 }

 $total = array();

 foreach ($anss as $rig => $ans)
 {
	if ($ans == null)
		continue;

	foreach ($ans as $item => $row)
	{
		if ($item == 'STATUS')
			continue;

		if (count($row) > count($header))
		{
			$header = $head;
			foreach ($row as $name => $value)
				if (!isset($header[$name]))
					$header[$name] = '';
		}

		if ($sum != null)
			foreach ($sum as $name)
			{
				if (isset($row[$name]))
				{
					if (isset($total[$name]))
						$total[$name] += $row[$name];
					else
						$total[$name] = $row[$name];
				}
			}
	}
 }

 if ($sum != null)
	$anss['total']['total'] = $total;

 showhead('', $header);

 foreach ($anss as $rig => $ans)
 {
	if ($ans == null)
		continue;

	$when = 0;
	if (isset($ans['STATUS']['When']))
		$when = $ans['STATUS']['When'];

	foreach ($ans as $item => $row)
	{
		if ($item == 'STATUS')
			continue;

		newrow();

		$section = preg_replace('/\d/', '', $item);

		foreach ($header as $name => $x)
		{
			if ($name == '')
			{
				if ($rig === 'total')
				{
					list($ignore, $class) = fmt($rig, '', '', $when, $row);
					echo "<td align=right$class>Total:</td>";
				}
				else
					echo rigbutton($rig, "Rig $rig", $when, $row);
			}
			else
			{
				if (isset($row[$name]))
					$value = $row[$name];
				else
					$value = null;

				list($showvalue, $class) = fmt($section, $name, $value, $when, $row);
				echo "<td$class align=right>$showvalue</td>";
			}
		}
		endrow();
	}
 }
}
#
function refreshbuttons()
{
 global $readonly;
 global $ignorerefresh, $changerefresh, $autorefresh;

 if ($ignorerefresh == false && $changerefresh == true)
 {
	echo '&nbsp;&nbsp;&nbsp;&nbsp;';
	echo "<input type=button value='Auto Refresh:' onclick='prr(true)'>";
	echo "<input type=text name='refval' id='refval' size=2 value='$autorefresh'>";
	echo "<input type=button value='Off' onclick='prr(false)'>";
 }
}
#
function pagebuttons($rig, $pg)
{
 global $readonly, $rigs;
 global $allowcustompages, $customsummarypages;

 if ($rig === null)
 {
	if ($pg === null)
		$refresh = '';
	else
		$refresh = "&pg=$pg";
 }
 else
	$refresh = "&rig=$rig";

 echo '<tr><td><table cellpadding=0 cellspacing=0 border=0><tr><td nowrap>';
 echo "<input type=button value='Refresh' onclick='pr(\"$refresh\",null)'>&nbsp;";
 if (count($rigs) > 1)
	echo "<input type=button value='Summary' onclick='pr(\"\",null)'>&nbsp;";

 if ($allowcustompages === true)
	foreach ($customsummarypages as $pagename => $data)
		echo "<input type=button value='$pagename' onclick='pr(\"&pg=$pagename\",null)'>&nbsp;";

 echo '</td><td width=100%>&nbsp;</td><td nowrap>';
 if ($rig !== null && $readonly === false)
 {
	$rg = '';
	if (count($rigs) > 1)
		$rg = " Rig $rig";
	echo "<input type=button value='Restart' onclick='prc(\"restart&rig=$rig\",\"Restart CGMiner$rg\")'>";
	echo "&nbsp;<input type=button value='Quit' onclick='prc(\"quit&rig=$rig\",\"Quit CGMiner$rg\")'>";
 }
 refreshbuttons();
 echo "</td></tr></table></td></tr>";
}
#
function doOne($rig, $preprocess)
{
 global $haderror, $readonly, $notify, $rigs;
 global $placebuttons;

 htmlhead(true, $rig);

 if ($placebuttons == 'top' || $placebuttons == 'both')
	pagebuttons($rig, null);

 if ($preprocess != null)
	process(array($preprocess => $preprocess), $rig);

 $cmds = array(	'devs'    => 'device list',
		'summary' => 'summary information',
		'pools'   => 'pool list');

 if ($notify)
	$cmds['notify'] = 'device status';

 $cmds['config'] = 'cgminer config';

 process($cmds, $rig);

 if ($haderror == false && $readonly === false)
	processgpus($rig);

 if ($placebuttons == 'bot' || $placebuttons == 'both')
	pagebuttons($rig, null);
}
#
global $sectionmap;
# map sections to their api command
# DEVS is a special case that will match GPU or PGA
# so you can have a single table with both in it
# DATE is hard coded so not in here
$sectionmap = array(
	'RIGS' => 'version',
	'SUMMARY' => 'summary',
	'POOL' => 'pools',
	'DEVS' => 'devs',
	'GPU' => 'devs',	// You would normally use DEVS
	'PGA' => 'devs',	// You would normally use DEVS
	'NOTIFY' => 'notify',
	'DEVDETAILS' => 'devdetails',
	'STATS' => 'stats',
	'CONFIG' => 'config',
	'COIN' => 'coin');
#
function joinfields($section1, $section2, $join, $results)
{
 global $sectionmap;

 $name1 = $sectionmap[$section1];
 $name2 = $sectionmap[$section2];
 $newres = array();

 // foreach rig in section1
 foreach ($results[$name1] as $rig => $result)
 {
	$status = null;

	// foreach answer section in the rig api call
	foreach ($result as $name1b => $fields1b)
	{
		if ($name1b == 'STATUS')
		{
			// remember the STATUS from section1
			$status = $result[$name1b];
			continue;
		}

		// foreach answer section in the rig api call (for the other api command)
		foreach ($results[$name2][$rig] as $name2b => $fields2b)
		{
			if ($name2b == 'STATUS')
				continue;

			// If match the same field values of fields in $join
			$match = true;
			foreach ($join as $field)
				if ($fields1b[$field] != $fields2b[$field])
				{
					$match = false;
					break;
				}

			if ($match === true)
			{
				if ($status != null)
				{
					$newres[$rig]['STATUS'] = $status;
					$status = null;
				}

				$subsection = $section1.'+'.$section2;
				$subsection .= preg_replace('/[^0-9]/', '', $name1b.$name2b);

				foreach ($fields1b as $nam => $val)
					$newres[$rig][$subsection]["$section1.$nam"] = $val;
				foreach ($fields2b as $nam => $val)
					$newres[$rig][$subsection]["$section2.$nam"] = $val;
			}
		}
	}
 }
 return $newres;
}
#
function joinall($section1, $section2, $results)
{
 global $sectionmap;

 $name1 = $sectionmap[$section1];
 $name2 = $sectionmap[$section2];
 $newres = array();

 // foreach rig in section1
 foreach ($results[$name1] as $rig => $result)
 {
	// foreach answer section in the rig api call
	foreach ($result as $name1b => $fields1b)
	{
		if ($name1b == 'STATUS')
		{
			// copy the STATUS from section1
			$newres[$rig][$name1b] = $result[$name1b];
			continue;
		}

		// foreach answer section in the rig api call (for the other api command)
		foreach ($results[$name2][$rig] as $name2b => $fields2b)
		{
			if ($name2b == 'STATUS')
				continue;

			$subsection = $section1.'+'.$section2;
			$subsection .= preg_replace('/[^0-9]/', '', $name1b.$name2b);

			foreach ($fields1b as $nam => $val)
				$newres[$rig][$subsection]["$section1.$nam"] = $val;
			foreach ($fields2b as $nam => $val)
				$newres[$rig][$subsection]["$section2.$nam"] = $val;
		}
	}
 }
 return $newres;
}
#
function joinsections($sections, $results, $errors)
{
 global $sectionmap;

#echo "results['pools']=".print_r($results['pools'],true)."<br>";

 // GPU's don't have Name,ID fields - so create them
 foreach ($results as $section => $res)
	foreach ($res as $rig => $result)
		foreach ($result as $name => $fields)
		{
			$subname = preg_replace('/[0-9]/', '', $name);
			if ($subname == 'GPU' and isset($result[$name]['GPU']))
			{
				$results[$section][$rig][$name]['Name'] = 'GPU';
				$results[$section][$rig][$name]['ID'] = $result[$name]['GPU'];
			}
		}

 foreach ($sections as $section => $fields)
	if ($section != 'DATE' && !isset($sectionmap[$section]))
	{
		$both = explode('+', $section, 2);
		if (count($both) > 1)
		{
			switch($both[0])
			{
			case 'SUMMARY':
				switch($both[1])
				{
				case 'POOL':
				case 'DEVS':
				case 'CONFIG':
				case 'COIN':
					$sectionmap[$section] = $section;
					$results[$section] = joinall($both[0], $both[1], $results);
					break;
				default:
					$errors[] = "Error: Invalid section '$section'";
					break;
				}
				break;
			case 'DEVS':
				$join = array('Name', 'ID');
				switch($both[1])
				{
				case 'NOTIFY':
				case 'DEVDETAILS':
					$sectionmap[$section] = $section;
					$results[$section] = joinfields($both[0], $both[1], $join, $results);
					break;
				default:
					$errors[] = "Error: Invalid section '$section'";
					break;
				}
				break;
			default:
				$errors[] = "Error: Invalid section '$section'";
				break;
			}
		}
		else
			$errors[] = "Error: Invalid section '$section'";
	}

 return array($results, $errors);
}
#
function secmatch($section, $field)
{
 if ($section == $field)
	return true;

 if ($section == 'DEVS'
 &&  ($field == 'GPU' || $field == 'PGA'))
	return true;

 return false;
}
#
function customset($showfields, $sum, $section, $rig, $isbutton, $result, $total)
{
 foreach ($result as $sec => $row)
 {
	$secname = preg_replace('/\d/', '', $sec);

	if ($sec != 'total')
		if (!secmatch($section, $secname))
			continue;

	newrow();

	$when = 0;
	if (isset($result['STATUS']['When']))
		$when = $result['STATUS']['When'];


	if ($isbutton)
		echo rigbutton($rig, $rig, $when, $row);
	else
	{
		list($ignore, $class) = fmt('total', '', '', $when, $row);
		echo "<td align=middle$class>$rig</td>";
	}

	foreach ($showfields as $name => $one)
	{
		if (isset($row[$name]))
		{
			$value = $row[$name];

			if (isset($sum[$section][$name]))
			{
				if (isset($total[$name]))
					$total[$name] += $value;
				else
					$total[$name] = $value;
			}
		}
		else
		{
			if ($sec == 'total' && isset($total[$name]))
				$value = $total[$name];
			else
				$value = null;
		}

		if (strpos($secname, '+') === false)
			list($showvalue, $class) = fmt($secname, $name, $value, $when, $row);
		else
		{
			$parts = explode('.', $name, 2);
			list($showvalue, $class) = fmt($parts[0], $parts[1], $value, $when, $row);
		}

		echo "<td$class align=right>$showvalue</td>";
	}
	endrow();
 }
 return $total;
}
#
function processcustompage($pagename, $sections, $sum, $namemap)
{
 global $sectionmap;
 global $miner, $port;
 global $rigs, $error;
 global $warnfont, $warnoff;
 global $dfmt;
 global $readonly, $showndate;

 $cmds = array();
 $errors = array();
 foreach ($sections as $section => $fields)
 {
	$all = explode('+', $section);
	foreach ($all as $section)
	{
		if (isset($sectionmap[$section]))
		{
			$cmd = $sectionmap[$section];
			if (!isset($cmds[$cmd]))
				$cmds[$cmd] = 1;
		}
		else
			if ($section != 'DATE')
				$errors[] = "Error: unknown section '$section' in custom summary page '$pagename'";
	}
 }

 $results = array();
 foreach ($rigs as $num => $rig)
 {
	$parts = explode(':', $rig, 3);
	if (count($parts) >= 2)
	{
		$miner = $parts[0];
		$port = $parts[1];

		if (count($parts) > 2)
			$name = $parts[2];
		else
			$name = $rig;

		foreach ($cmds as $cmd => $one)
		{
			$process = api($name, $cmd);

			if ($error != null)
			{
				$errors[] = "Error getting $cmd for $name $warnfont$error$warnoff";
				break;
			}
			else
				$results[$cmd][$num] = $process;
		}
	}
 }

 $shownsomething = false;
 if (count($results) > 0)
 {
	list($results, $errors) = joinsections($sections, $results, $errors);
	$first = true;
	foreach ($sections as $section => $fields)
	{
		if ($section === 'DATE')
		{
			if ($shownsomething)
				otherrow('<td>&nbsp;</td>');

			newtable();
			showdatetime();
			endtable();
			// On top of the next table
			$shownsomething = false;
			continue;
		}

		if ($section === 'RIGS')
		{
			if ($shownsomething)
				otherrow('<td>&nbsp;</td>');

			newtable();
			showrigs($results['version'], 'Rig', '');
			endtable();
			$shownsomething = true;
			continue;
		}

		if (isset($results[$sectionmap[$section]]))
		{
			$rigresults = $results[$sectionmap[$section]];
			$showfields = array();
			$showhead = array();
			foreach ($fields as $field)
				foreach ($rigresults as $result)
					foreach ($result as $sec => $row)
					{
						$secname = preg_replace('/\d/', '', $sec);
						if (secmatch($section, $secname))
						{
							if ($field === '*')
							{
								foreach ($row as $f => $v)
								{
									$showfields[$f] = 1;
									$map = $section.'.'.$f;
									if (isset($namemap[$map]))
										$showhead[$namemap[$map]] = 1;
									else
										$showhead[$f] = 1;
								}
							}
							elseif (isset($row[$field]))
							{
								$showfields[$field] = 1;
								$map = $section.'.'.$field;
								if (isset($namemap[$map]))
									$showhead[$namemap[$map]] = 1;
								else
									$showhead[$field] = 1;
							}
						}
					}

			if (count($showfields) > 0)
			{
				if ($shownsomething)
					otherrow('<td>&nbsp;</td>');

				newtable();
				showhead('', array('Rig'=>1)+$showhead, true);

				$total = array();
				$add = array('total' => array());

				foreach ($rigresults as $num => $result)
					$total = customset($showfields, $sum, $section, $num, true, $result, $total);

				if (count($total) > 0)
					customset($showfields, $sum, $section, '&Sigma;', false, $add, $total);

				$first = false;

				endtable();
				$shownsomething = true;
			}
		}
	}
 }

 if (count($errors) > 0)
 {
	if (count($results) > 0)
		otherrow('<td>&nbsp;</td>');

	foreach ($errors as $err)
		otherrow("<td colspan=100>$err</td>");
 }
}
#
function showcustompage($pagename)
{
 global $customsummarypages;
 global $placebuttons;

 htmlhead(false, null, $pagename);

 if ($placebuttons == 'top' || $placebuttons == 'both')
	pagebuttons(null, $pagename);

 if (!isset($customsummarypages[$pagename]))
 {
	otherrow("<td colspan=100>Unknown custom summary page '$pagename'</td>");
	return;
 }

 if (count($customsummarypages[$pagename]) != 2)
 {
	$rw = "<td colspan=100>Invalid custom summary page '$pagename' (";
	$rw .= count($customsummarypages[$pagename]).')</td>';
	otherrow($rw);
	return;
 }

 $page = $customsummarypages[$pagename][0];
 $namemap = array();
 foreach ($page as $name => $fields)
 {
	if ($fields === null)
		$page[$name] = array();
	else
		foreach ($fields as $num => $field)
		{
			$pos = strpos($field, '=');
			if ($pos !== false)
			{
				$names = explode('=', $field, 2);
				if (strlen($names[1]) > 0)
					$namemap[$name.'.'.$names[0]] = $names[1];
				$page[$name][$num] = $names[0];
			}
		}
 }

 $sum = $customsummarypages[$pagename][1];
 if ($sum === null)
	$sum = array();

 // convert them to searchable via isset()
 foreach ($sum as $section => $fields)
 {
	$newfields = array();
	foreach ($fields as $field)
		$newfields[$field] = 1;
	$sum[$section] = $newfields;
 }

 if (count($page) <= 1)
 {
	otherrow("<td colspan=100>Invalid custom summary page '$pagename' no content </td>");
	return;
 }

 processcustompage($pagename, $page, $sum, $namemap);

 if ($placebuttons == 'bot' || $placebuttons == 'both')
	pagebuttons(null, $pagename);
}
#
function display()
{
 global $miner, $port;
 global $readonly, $notify, $rigs;
 global $ignorerefresh, $autorefresh;
 global $allowcustompages;
 global $placebuttons;

 if ($ignorerefresh == false)
 {
	$ref = trim(getparam('ref', true));
	if ($ref != null && $ref != '')
		$autorefresh = intval($ref);
 }

 $rig = trim(getparam('rig', true));

 $arg = trim(getparam('arg', true));
 $preprocess = null;
 if ($arg != null and $arg != '')
 {
	$num = null;
	if ($rig != null and $rig != '')
	{
		if ($rig >= 0 and $rig < count($rigs))
			$num = $rig;
	}
	else
		if (count($rigs) == 0)
			$num = 0;

	if ($num != null)
	{
		$parts = explode(':', $rigs[$num], 3);
		if (count($parts) >= 2)
		{
			$miner = $parts[0];
			$port = $parts[1];

			if ($readonly !== true)
				$preprocess = $arg;
		}
	}
 }

 if ($rigs == null or count($rigs) == 0)
 {
	otherrow("<td>No rigs defined</td>");
	return;
 }

 if ($allowcustompages === true)
 {
	$pg = trim(getparam('pg', true));
	if ($pg != null && $pg != '')
	{
		showcustompage($pg);
		return;
	}
 }

 if (count($rigs) == 1)
 {
	$parts = explode(':', $rigs[0], 3);
	if (count($parts) >= 2)
	{
		$miner = $parts[0];
		$port = $parts[1];

		doOne(0, $preprocess);
	}
	else
		otherrow('<td>Invalid "$rigs" array</td>');

	return;
 }

 if ($rig != null and $rig != '' and $rig >= 0 and $rig < count($rigs))
 {
	$parts = explode(':', $rigs[$rig], 3);
	if (count($parts) >= 2)
	{
		$miner = $parts[0];
		$port = $parts[1];

		doOne($rig, $preprocess);
	}
	else
		otherrow('<td>Invalid "$rigs" array</td>');

	return;
 }

 htmlhead(false, null);

 if ($placebuttons == 'top' || $placebuttons == 'both')
	pagebuttons(null, null);

 if ($preprocess != null)
	process(array($preprocess => $preprocess), $rig);

 newtable();
 doforeach('version', 'rig summary', array(), array(), true);
 $sum = array('MHS av', 'Getworks', 'Found Blocks', 'Accepted', 'Rejected', 'Discarded', 'Stale', 'Utility', 'Local Work', 'Total MH', 'Work Utility', 'Diff1 Shares', 'Diff1 Work');
 doforeach('summary', 'summary information', $sum, array(), false);
 endtable();
 otherrow('<td><br><br></td>');
 newtable();
 doforeach('devs', 'device list', $sum, array(''=>'','ID'=>'','Name'=>''), false);
 endtable();
 otherrow('<td><br><br></td>');
 newtable();
 doforeach('pools', 'pool list', $sum, array(''=>''), false);
 endtable();

 if ($placebuttons == 'bot' || $placebuttons == 'both')
	pagebuttons(null, null);
}
#
display();
#
?>
</table></td></tr></table>
</body></html>
