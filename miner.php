<?php
session_start();
#
global $miner, $port, $readonly, $notify, $rigs, $socktimeoutsec;
global $checklastshare, $hidefields;
global $ignorerefresh, $changerefresh, $autorefresh;
global $allowcustompages, $customsummarypages;
global $miner_font_family, $miner_font_size;
global $colouroverride, $placebuttons;
#
# Don't touch these 2 - see $rigs below
$miner = null;
$port = null;
#
# Set $readonly to true to force miner.php to be readonly
# Set $readonly to false then it will check cgminer 'privileged'
$readonly = false;
#
# Set $notify to false to NOT attempt to display the notify command
# Set $notify to true to attempt to display the notify command
# If your older version of cgminer returns an 'Invalid command'
#  coz it doesn't have notify - it just shows the error status table
$notify = true;
#
# set $checklastshare to true to do the following checks:
# If a device's last share is 12x expected ago then display as an error
# If a device's last share is 8x expected ago then display as a warning
# If either of the above is true, also display the whole line highlighted
# This assumes shares are 1 difficulty shares
$checklastshare = true;
#
# Set $rigs to an array of your cgminer rigs that are running
#  format: 'IP:Port' or 'Host:Port'
# If you only have one rig, it will just show the detail of that rig
# If you have more than one rig it will show a summary of all the rigs
#  with buttons to show the details of each rig
# e.g. $rigs = array('127.0.0.1:4028','myrig.com:4028');
$rigs = array('127.0.0.1:4028');
#
# This should be OK for most cases
# However, the longer it is the longer you have to wait while php
# hangs if the target cgminer isn't runnning or listening
# Feel free to increase it if your network is very slow
# Also, on some windows PHP, apparently the $usec is ignored
$socktimeoutsec = 10;
#
# List of fields NOT to be displayed
# You can use this to hide data you don't want to see or don't want
# shown on a public web page
# The list of sections are: SUMMARY, POOL, PGA, GPU, NOTIFY, CONFIG
# See the web page for the list of field names (the table headers)
# It is an array of 'SECTION.Field Name' => 1
# This example would hide the slightly more sensitive pool information
#$hidefields = array('POOL.URL' => 1, 'POOL.User' => 1);
$hidefields = array();
#
# Auto-refresh of the page (in seconds)
# $ignorerefresh = true/false always ignore refresh parameters
# $changerefresh = true/false show buttons to change the value
# $autorefresh = default value, 0 means dont auto-refresh
$ignorerefresh = false;
$changerefresh = true;
$autorefresh = 0;
#
# Should we allow custom pages?
# (or just completely ignore then and don't display the buttons)
$allowcustompages = true;
#
# OK this is a bit more complex item: Custom Summary Pages
# A custom summary page in an array of 'section' => array('FieldA','FieldB'...)
# This makes up what is displayed with each 'section' separately as a table
# - empty tables are not shown
# - empty columns (an unknown field) are not shown
# - and missing field data shows as blank
# There is a second array, listing fields to be totaled for each section
# see the example below (if there is no matching data, no total will show)
$mobilepage = array(
 'SUMMARY' => array('Elapsed', 'MHS av', 'Found Blocks', 'Accepted', 'Rejected', 'Utility'),
 'DEVS' => array('ID', 'Name', 'GPU', 'Status', 'MHS av', 'Accepted', 'Rejected', 'Utility'),
 'POOL' => array('POOL', 'Status', 'Accepted', 'Rejected', 'Last Share Time'));
$mobilesum = array(
 'SUMMARY' => array('MHS av', 'Found Blocks', 'Accepted', 'Rejected', 'Utility'),
 'DEVS' => array('MHS av', 'Accepted', 'Rejected', 'Utility'),
 'POOL' => array('Accepted', 'Rejected'));
#
# customsummarypages is an array of these Custom Summary Pages
$customsummarypages = array('Mobile' => array($mobilepage, $mobilesum));
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
# Ensure it is only ever shown once
global $showndate;
$showndate = false;
#
# For summary page to stop retrying failed rigs
global $rigerror;
$rigerror = array();
#
function getcss($cssname, $dom = false)
{
 global $colourtable, $colouroverride;

 $css = '';
 foreach ($colourtable as $cssdata => $value)
 {
	$cssobj = split(' ', $cssdata, 2);
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
 global $miner_font_family, $miner_font_size;
 global $error, $readonly, $here;
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
	$access = api('privileged');
	if ($error != null
	||  !isset($access['STATUS']['STATUS'])
	||  $access['STATUS']['STATUS'] != 'S')
		$readonly = true;
 }
 $miner_font = "font-family:$miner_font_family; font-size:$miner_font_size;";

 echo "<html><head>$refreshmeta
<title>Mine</title>
<style type='text/css'>
td { $miner_font ".getcss('td')."}
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
function getsock($addr, $port)
{
 global $haderror, $error, $socktimeoutsec;

 $error = null;
 $socket = null;
 $socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
 if ($socket === false || $socket === null)
 {
	$haderror = true;
	$error = socket_strerror(socket_last_error());
	$msg = "socket create(TCP) failed";
	$error = "ERR: $msg '$error'\n";
	return null;
 }

 // Ignore if this fails since the socket connect may work anyway
 //  and nothing is gained by aborting if the option cannot be set
 //  since we don't know in advance if it can connect
 socket_set_option($socket, SOL_SOCKET, SO_SNDTIMEO, array('sec' => $socktimeoutsec, 'usec' => 0));

 $res = socket_connect($socket, $addr, $port);
 if ($res === false)
 {
	$haderror = true;
	$error = socket_strerror(socket_last_error());
	$msg = "socket connect($addr,$port) failed";
	$error = "ERR: $msg '$error'\n";
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
function api($cmd)
{
 global $haderror, $error;
 global $miner, $port, $hidefields;

 $socket = getsock($miner, $port);
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
					$data[$name][$id[0]] = $id[1];
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
function classlastshare($when, $alldata, $warnclass, $errorclass)
{
 global $checklastshare;

 if ($checklastshare === false)
	return '';

 if ($when == 0)
	return '';

 if (!isset($alldata['MHS av']))
	return '';

 if (!isset($alldata['Last Share Time']))
	return '';

 $expected = pow(2, 32) / ($alldata['MHS av'] * pow(10, 6));
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
 global $dfmt;

 if ($alldata == null)
	$alldata = array();

 $errorclass = ' class=err';
 $warnclass = ' class=warn';
 $lstclass = ' class=lst';
 $hiclass = ' class=hi';
 $loclass = ' class=lo';
 $b = '&nbsp;';

 $ret = $value;
 $class = '';

 if ($value === null)
	$ret = $b;
 else
	switch ($section.'.'.$name)
	{
	case 'GPU.Last Share Time':
	case 'PGA.Last Share Time':
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
	case 'SUMMARY.Utility':
	case 'total.Utility':
		$ret = $value.'/m';
		if ($value == 0)
			$class = $errorclass;
		else
			if (isset($alldata['MHS av']))
			{
				$expected = 60 * $alldata['MHS av'] * (pow(10, 6) / pow(2, 32));
				if ($expected == 0)
					$expected = 0.000001; // 1 H/s
				$ratio = $value / $expected;
				if ($ratio < 0.9)
					$class = $loclass;
				else
					if ($ratio > 1.1)
						$class = $hiclass;
			}
		break;
	case 'PGA.Temperature':
		$ret = $value.'&deg;C';
		break;
	case 'GPU.Temperature':
		$ret = $value.'&deg;C';
	case 'GPU.GPU Clock':
	case 'GPU.Memory Clock':
	case 'GPU.GPU Voltage':
	case 'GPU.GPU Activity':
		if ($value == 0)
			$class = $warnclass;
		break;
	case 'GPU.Fan Percent':
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
	case 'SUMMARY.MHS av':
	case 'total.MHS av':
		$parts = explode('.', $value, 2);
		if (count($parts) == 1)
			$dec = '';
		else
			$dec = '.'.$parts[1];
		$ret = number_format($parts[0]).$dec;

		if ($value == 0)
			$class = $errorclass;
		else
			if (isset($alldata['Utility']))
			{
				$expected = 60 * $value * (pow(10, 6) / pow(2, 32));
				$utility = $alldata['Utility'];
				$ratio = $utility / $expected;
				if ($ratio < 0.9)
					$class = $hiclass;
				else
					if ($ratio > 1.1)
						$class = $loclass;
			}
		break;
	case 'GPU.Total MH':
	case 'PGA.Total MH':
	case 'SUMMARY.Total MH':
	case 'total.Total MH':
	case 'SUMMARY.Getworks':
	case 'POOL.Getworks':
	case 'total.Getworks':
	case 'GPU.Accepted':
	case 'PGA.Accepted':
	case 'SUMMARY.Accepted':
	case 'POOL.Accepted':
	case 'total.Accepted':
	case 'GPU.Rejected':
	case 'PGA.Rejected':
	case 'SUMMARY.Rejected':
	case 'POOL.Rejected':
	case 'total.Rejected':
	case 'SUMMARY.Local Work':
	case 'total.Local Work':
	case 'SUMMARY.Discarded':
	case 'POOL.Discarded':
	case 'total.Discarded':
		$parts = explode('.', $value, 2);
		if (count($parts) == 1)
			$dec = '';
		else
			$dec = '.'.$parts[1];
		$ret = number_format($parts[0]).$dec;
		break;
	case 'GPU.Status':
	case 'PGA.Status':
	case 'POOL.Status':
		if ($value != 'Alive')
			$class = $errorclass;
		break;
	case 'GPU.Enabled':
	case 'PGA.Enabled':
		if ($value != 'Y')
			$class = $warnclass;
		break;
	case 'STATUS.When':
		$ret = date($dfmt, $value);
		break;
	}

 if ($section == 'NOTIFY' && substr($name, 0, 1) == '*' && $value != '0')
	$class = $errorclass;

 if ($class == '' && $section != 'POOL')
	$class = classlastshare($when, $alldata, $lstclass, $lstclass);

 return array($ret, $class);
}
#
global $poolcmd;
$poolcmd = array(	'Switch to'	=> 'switchpool',
			'Enable'	=> 'enablepool',
			'Disable'	=> 'disablepool' );
#
function showhead($cmd, $values, $justnames = false)
{
 global $poolcmd, $readonly;

 echo '<tr>';

 foreach ($values as $name => $value)
 {
	if ($name == '0' or $name == '')
		$name = '&nbsp;';
	echo "<td valign=bottom class=h>$name</td>";
 }

 if ($justnames === false && $cmd == 'pools' && $readonly === false)
	foreach ($poolcmd as $name => $pcmd)
		echo "<td valign=bottom class=h>$name</td>";

 echo '</tr>';
}
#
function details($cmd, $list, $rig)
{
 global $tablebegin, $tableend, $dfmt;
 global $poolcmd, $readonly;
 global $showndate;

 $when = 0;

 $stas = array('S' => 'Success', 'W' => 'Warning', 'I' => 'Informational', 'E' => 'Error', 'F' => 'Fatal');

 echo $tablebegin;

 if ($showndate === false)
 {
	echo '<tr><td class=sta>Date: '.date($dfmt).'</td></tr>';

	echo $tableend.$tablebegin;

	$showndate = true;
 }

 if (isset($list['STATUS']))
 {
	echo '<tr>';
	echo '<td>Computer: '.$list['STATUS']['Description'].'</td>';
	if (isset($list['STATUS']['When']))
	{
		echo '<td>When: '.date($dfmt, $list['STATUS']['When']).'</td>';
		$when = $list['STATUS']['When'];
	}
	$sta = $list['STATUS']['STATUS'];
	echo '<td>Status: '.$stas[$sta].'</td>';
	echo '<td>Message: '.$list['STATUS']['Msg'].'</td>';
	echo '</tr>';
 }


 $section = '';

 foreach ($list as $item => $values)
 {
	if ($item == 'STATUS')
		continue;

	$sectionname = preg_replace('/\d/', '', $item);

	if ($sectionname != $section)
	{
		echo $tableend.$tablebegin;
		showhead($cmd, $values);
		$section = $sectionname;
	}

	echo '<tr>';

	foreach ($values as $name => $value)
	{
		list($showvalue, $class) = fmt($section, $name, $value, $when, $values);
		echo "<td$class>$showvalue</td>";
	}

	if ($cmd == 'pools' && $readonly === false)
	{
		reset($values);
		$pool = current($values);
		foreach ($poolcmd as $name => $pcmd)
		{
			echo '<td>';
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

	echo '</tr>';
 }

 echo $tableend;
}
#
global $devs;
$devs = null;
#
function gpubuttons($count, $rig)
{
 global $tablebegin, $tableend;
 global $devs;

 $basic = array( 'GPU', 'Enable', 'Disable', 'Restart' );

 $options = array(	'intensity' => 'Intensity',
			'fan' => 'Fan Percent',
			'engine' => 'GPU Clock',
			'mem' => 'Memory Clock',
			'vddc' => 'GPU Voltage' );

 echo $tablebegin.'<tr>';

 foreach ($basic as $head)
	echo "<td>$head</td>";

 foreach ($options as $name => $des)
	echo "<td nowrap>$des</td>";

 $n = 0;
 for ($c = 0; $c < $count; $c++)
 {
	echo '</tr><tr>';

	foreach ($basic as $name)
	{
		echo '<td>';

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
		echo '<td>';
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

 echo '</tr>'.$tableend;
}
#
function processgpus($rig)
{
 global $error;
 global $warnfont, $warnoff;

 $gpus = api('gpucount');

 if ($error != null)
	echo '<tr><td>Error getting GPU count: '.$warnfont.$error.$warnoff.'</td></tr>';
 else
 {
	if (!isset($gpus['GPUS']['Count']))
		echo '<tr><td>No GPU count returned: '.$warnfont.$gpus['STATUS']['STATUS'].' '.$gpus['STATUS']['Msg'].$ro.'</td></tr>';
	else
	{
		$count = $gpus['GPUS']['Count'];
		if ($count == 0)
			echo '<tr><td>No GPUs</td></tr>';
		else
			gpubuttons($count, $rig);
	}
 }
}
#
function process($cmds, $rig)
{
 global $error, $devs;
 global $warnfont, $warnoff;

 $count = count($cmds);
 foreach ($cmds as $cmd => $des)
 {
	$process = api($cmd);

	if ($error != null)
	{
		echo "<tr><td colspan=100>Error getting $des: ";
		echo $warnfont.$error.$warnoff.'</td></tr>';
		break;
	}
	else
	{
		details($cmd, $process, $rig);

		# Not after the last one
		if (--$count > 0)
			echo '<tr><td><br><br></td></tr>';

		if ($cmd == 'devs')
			$devs = $process;
	}
 }
}
#
# $head is a hack but this is just a demo anyway :)
function doforeach($cmd, $des, $sum, $head, $datetime)
{
 global $miner, $port;
 global $error, $readonly, $notify, $rigs;
 global $tablebegin, $tableend, $warnfont, $warnoff, $dfmt;
 global $rigerror;

 $when = 0;

 $header = $head;
 $anss = array();

 $count = 0;
 $preverr = count($rigerror);
 foreach ($rigs as $rig)
 {
	if (isset($rigerror[$rig]))
		continue;

	$parts = explode(':', $rig, 2);
	if (count($parts) == 2)
	{
		$miner = $parts[0];
		$port = $parts[1];

		$ans = api($cmd);

		if ($error != null)
		{
			echo "<tr><td colspan=100>Error on rig $count getting $des: ";
			echo $warnfont.$error.$warnoff.'</td></tr>';
			$rigerror[$rig] = $error;
			$error = null;
		}
		else
			$anss[$count] = $ans;
	}
	$count++;
 }

 if (count($anss) == 0)
 {
	echo '<tr><td>Failed to access any rigs successfully';
	if ($preverr > 0)
		echo ' (or rigs had previous errors)';
	echo '</td></tr>';
	return;
 }

 if ($datetime)
 {
	echo '<tr><td class=sta>Date: '.date($dfmt).'</td></tr>';

	echo $tableend.$tablebegin;

	$dthead = array('' => 1, 'STATUS' => 1, 'Description' => 1, 'When' => 1, 'API' => 1, 'CGMiner' => 1);
	showhead('', $dthead);

	foreach ($anss as $rig => $ans)
	{
		echo '<tr>';

		foreach ($ans as $item => $row)
		{
			if ($item != 'STATUS' && $item != 'VERSION')
				continue;

			foreach ($dthead as $name => $x)
			{
				if ($item == 'STATUS' && $name == '')
					echo "<td align=right><input type=button value='Rig $rig' onclick='pr(\"&rig=$rig\",null)'></td>";
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

		echo '</tr>';
	}
	echo $tableend;
	echo '<tr><td><br><br></td></tr>';
	echo $tablebegin;

	return;
 }

 $total = array();

 foreach ($anss as $rig => $ans)
 {
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
	$when = 0;
	if (isset($ans['STATUS']['When']))
		$when = $ans['STATUS']['When'];

	foreach ($ans as $item => $row)
	{
		if ($item == 'STATUS')
			continue;

		echo '<tr>';

		$section = preg_replace('/\d/', '', $item);

		foreach ($header as $name => $x)
		{
			if ($name == '')
			{
				if ($rig === 'total')
					echo "<td align=right class=tot>Total:</td>";
				else
					echo "<td align=right><input type=button value='Rig $rig' onclick='pr(\"&rig=$rig\",null)'></td>";
			}
			else
			{
				if (isset($row[$name]))
					$value = $row[$name];
				else
					$value = null;

				list($showvalue, $class) = fmt($section, $name, $value, $when, $row);

				if ($rig === 'total' and $class == '')
					$class = ' class=tot';

				echo "<td$class align=right>$showvalue</td>";
			}
		}

		echo '</tr>';
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
$sectionmap = array(
	'SUMMARY' => 'summary',
	'POOL' => 'pools',
	'DEVS' => 'devs',
	'GPU' => 'devs',	// You would normally use DEVS
	'PGA' => 'devs',	// You would normally use DEVS
	'NOTIFY' => 'notify',
	'CONFIG' => 'config');
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
function customset($showfields, $sum, $section, $num, $result, $total)
{
 foreach ($result as $sec => $row)
 {
	$secname = preg_replace('/\d/', '', $sec);

	if ($sec != 'total')
		if (!secmatch($section, $secname))
			continue;

	echo '<tr>';

	$when = 0;
	if (isset($result['STATUS']['When']))
		$when = $result['STATUS']['When'];

	if ($sec === 'total')
		$class = ' class=tot';
	else
		$class = '';

	echo "<td align=middle$class>$num</td>";

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

		list($showvalue, $class) = fmt($secname, $name, $value, $when, $row);

		if ($sec === 'total' and $class == '')
			$class = ' class=tot';


		echo "<td$class align=right>$showvalue</td>";
	}

	echo '</tr>';
 }
 return $total;
}
#
function processcustompage($pagename, $sections, $sum)
{
 global $sectionmap;
 global $miner, $port;
 global $rigs, $error;
 global $warnfont, $warnoff;
 global $tablebegin, $tableend, $dfmt;
 global $readonly, $showndate;

 $cmds = array();
 $errors = array();
 foreach ($sections as $section => $fields)
 {
	if (isset($sectionmap[$section]))
	{
		$cmd = $sectionmap[$section];
		if (!isset($cmds[$cmd]))
			$cmds[$cmd] = 1;
	}
	else
		$errors[] = "Error: unknown section '$section' in custom summary page '$pagename'";
 }

 $results = array();
 foreach ($rigs as $num => $rig)
 {
	$parts = explode(':', $rig, 2);
	if (count($parts) == 2)
	{
		$miner = $parts[0];
		$port = $parts[1];

		foreach ($cmds as $cmd => $one)
		{
			$process = api($cmd);

			if ($error != null)
			{
				$errors[] = "Error getting $cmd for $rig $warnfont$error$warnoff";
				break;
			}
			else
				$results[$cmd][$num] = $process;
		}
	}
 }

 if (count($results) > 0)
 {
	$first = true;
	foreach ($sections as $section => $fields)
	{
		if (isset($results[$sectionmap[$section]]))
		{
			$rigresults = $results[$sectionmap[$section]];
			$showfields = array();
			foreach ($fields as $field)
				foreach ($rigresults as $result)
					foreach ($result as $sec => $row)
					{
						$secname = preg_replace('/\d/', '', $sec);
						if (secmatch($section, $secname) && isset($row[$field]))
							$showfields[$field] = 1;
					}

			if (count($showfields) > 0)
			{
				if ($first === false)
					echo '<tr><td>&nbsp;</td></tr>';

				echo $tablebegin;

				showhead('', array('Rig'=>1)+$showfields, true);

				$total = array();
				$add = array('total' => array());

				foreach ($rigresults as $num => $result)
				{
					$rg = "<input type=button value='$num' onclick='pr(\"&rig=$num\",null)'>";
					$total = customset($showfields, $sum, $section, $rg, $result, $total);
				}

				if (count($total) > 0)
					customset($showfields, $sum, $section, '&Sigma;', $add, $total);

				$first = false;

				echo $tableend;
			}
		}
	}
 }

 if (count($errors) > 0)
 {
	if (count($results) > 0)
		echo '<tr><td>&nbsp;</td></tr>';

	foreach ($errors as $err)
		echo "<tr><td colspan=100>$err</td></tr>";
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
	echo "<tr><td colspan=100>Unknown custom summary page '$pagename'</td></tr>";
	return;
 }

 if (count($customsummarypages[$pagename]) != 2)
 {
	echo "<tr><td colspan=100>Invalid custom summary page '$pagename' (".count($customsummarypages[$pagename]).")</td></tr>";
	return;
 }

 $page = $customsummarypages[$pagename][0];
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
	echo "<tr><td colspan=100>Invalid custom summary page '$pagename' no content </td></tr>";
	return;
 }

 processcustompage($pagename, $page, $sum);

 if ($placebuttons == 'bot' || $placebuttons == 'both')
	pagebuttons(null, $pagename);
}
#
function display()
{
 global $tablebegin, $tableend;
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
		$parts = explode(':', $rigs[$num], 2);
		if (count($parts) == 2)
		{
			$miner = $parts[0];
			$port = $parts[1];

			$preprocess = $arg;
		}
	}
 }

 if ($rigs == null or count($rigs) == 0)
 {
	echo "<tr><td>No rigs defined</td></tr>";
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
	$parts = explode(':', $rigs[0], 2);
	if (count($parts) == 2)
	{
		$miner = $parts[0];
		$port = $parts[1];

		doOne(0, $preprocess);
	}
	else
		echo '<tr><td>Invalid "$rigs" array</td></tr>';

	return;
 }

 if ($rig != null and $rig != '' and $rig >= 0 and $rig < count($rigs))
 {
	$parts = explode(':', $rigs[$rig], 2);
	if (count($parts) == 2)
	{
		$miner = $parts[0];
		$port = $parts[1];

		doOne($rig, $preprocess);
	}
	else
		echo '<tr><td>Invalid "$rigs" array</td></tr>';

	return;
 }

 htmlhead(false, null);

 if ($placebuttons == 'top' || $placebuttons == 'both')
	pagebuttons(null, null);

 if ($preprocess != null)
	process(array($preprocess => $preprocess), $rig);

 echo $tablebegin;
 doforeach('version', 'rig summary', array(), array(), true);
 $sum = array('MHS av', 'Getworks', 'Found Blocks', 'Accepted', 'Rejected', 'Discarded', 'Stale', 'Utility', 'Local Work', 'Total MH');
 doforeach('summary', 'summary information', $sum, array(), false);
 echo $tableend;
 echo '<tr><td><br><br></td></tr>';
 echo $tablebegin;
 doforeach('devs', 'device list', $sum, array(''=>'','ID'=>'','Name'=>''), false);
 echo $tableend;
 echo '<tr><td><br><br></td></tr>';
 echo $tablebegin;
 doforeach('pools', 'pool list', $sum, array(''=>''), false);
 echo $tableend;

 if ($placebuttons == 'bot' || $placebuttons == 'both')
	pagebuttons(null, null);
}
#
display();
#
?>
</table></td></tr></table>
</body></html>
