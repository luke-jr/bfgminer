<?php
session_start();
#
global $miner, $port;
$miner = '127.0.0.1'; # hostname or IP address
$port = 4028;
#
$here = $_SERVER['PHP_SELF'];
#
?>
<html><head><title>Mine</title>
<style type='text/css'>
td { color:blue; font-family:verdana,arial,sans; font-size:13pt; }
td.h { color:blue; font-family:verdana,arial,sans; font-size:13pt; background:#d0ffff }
td.sta { color:green; font-family:verdana,arial,sans; font-size:13pt; }
</style>
</head><body bgcolor=#ecffff>
<script type='text/javascript'>
function pr(a,m){if(m!=null){if(!confirm(m+'?'))return}window.location="<? echo $here ?>"+a}
function prc(a,m){pr('?arg='+a,m)}
function prs(a){var c=a.substr(3);var z=c.split('|',2);var m=z[0].substr(0,1).toUpperCase()+z[0].substr(1)+' GPU '+z[1];prc(a,m)}
function prs2(a,n){var v=document.getElementById('gi'+n).value;var c=a.substr(3);var z=c.split('|',2);var m='Set GPU '+z[1]+' '+z[0].substr(0,1).toUpperCase()+z[0].substr(1)+' to '+v;prc(a+','+v,m)}
</script>
<table width=100% height=100% border=0 cellpadding=0 cellspacing=0 summary='Mine'>
<tr><td align=center valign=top>
<table border=0 cellpadding=4 cellspacing=0 summary='Mine'>
<?
#
global $error;
$error = null;
#
function getsock($addr, $port)
{
 global $error;

 $socket = null;
 $socket = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
 if ($socket === false || $socket === null)
 {
	$error = socket_strerror(socket_last_error());
	$msg = "socket create(TCP) failed";
	$error = "ERR: $msg '$error'\n";
	return null;
 }

 $res = socket_connect($socket, $addr, $port);
 if ($res === false)
 {
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
 global $miner, $port;

 $socket = getsock($miner, $port);
 if ($socket != null)
 {
	socket_write($socket, $cmd, strlen($cmd));
	$line = readsockline($socket);
	socket_close($socket);

	if (strlen($line) == 0)
	{
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
function fmt($section, $name, $value)
{
 $b = '&nbsp;';

 switch ($section.'.'.$name)
 {
 case 'GPU0.Last Share Time':
	return date('H:i:s', $value);
	break;
 case 'SUMMARY.Elapsed':
	$s = $value % 60;
	$value -= $s;
	$value /= 60;
	if ($value == 0)
	{
		return $s.'s';
	}
	else
	{
		$m = $value % 60;
		$value -= $m;
		$value /= 60;
		if ($value == 0)
		{
			return sprintf("%dm$b%02ds", $m, $s);
		}
		else
		{
			$h = $value % 24;
			$value -= $h;
			$value /= 24;
			if ($value == 0)
				return sprintf("%dh$b%02dm$b%02ds", $h, $m, $s);
			else
				return sprintf("%ddays$b%02dh$b%02dm$b%02ds", $value, $h, $m, $s);
		}
	}
	break;
 case 'GPU0.Utility':
 case 'SUMMARY.Utility':
	return $value.'/m';
	break;
 case 'GPU0.Temperature':
	return $value.'&deg;C';
	break;
 }

 return $value;
}
#
function details($cmd, $list)
{
 $stas = array('S' => 'Success', 'W' => 'Warning', 'I' => 'Informational', 'E' => 'Error', 'F' => 'Fatal');

 $tb = '<tr><td><table border=1 cellpadding=5 cellspacing=0>';
 $te = '</table></td></tr>';

 echo $tb;

 echo '<tr><td class=sta>Date: '.date('H:i:s j-M-Y \U\T\CP').'</td></tr>';

 echo $te.$tb;

 if (isset($list['STATUS']))
 {
	echo '<tr>';
	echo '<td>Computer: '.$list['STATUS']['Description'].'</td>';
	$sta = $list['STATUS']['STATUS'];
	echo '<td>Status: '.$stas[$sta].'</td>';
	echo '<td>Message: '.$list['STATUS']['Msg'].'</td>';
	echo '</tr>';
 }

 echo $te.$tb;

 $section = '';

 $poolcmd = array(	'Switch to'	=> 'switchpool',
			'Enable'	=> 'enablepool',
			'Disable'	=> 'disablepool' );

 foreach ($list as $item => $values)
 {
	if ($item != 'STATUS')
	{
		$section = $item;

		echo '<tr>';

		foreach ($values as $name => $value)
		{
			if ($name == '0')
				$name = '&nbsp;';
			echo "<td valign=bottom class=h>$name</td>";
		}

		if ($cmd == 'pools')
			foreach ($poolcmd as $name => $pcmd)
				echo "<td valign=bottom class=h>$name</td>";

		echo '</tr>';

		break;
	}
 }

 foreach ($list as $item => $values)
 {
	if ($item == 'STATUS')
		continue;

	echo '<tr>';

	foreach ($values as $name => $value)
		echo '<td>'.fmt($section, $name, $value).'</td>';

	if ($cmd == 'pools')
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
				echo " onclick='prc(\"$pcmd|$pool\",\"$name Pool $pool\")'>";
			}
			echo '</td>';
		}
	}

	echo '</tr>';
 }
 echo $te;
}
#
global $devs;
$devs = null;
#
function gpubuttons($count, $info)
{
 global $devs;

 $basic = array( 'GPU', 'Enable', 'Disable', 'Restart' );

 $options = array(	'intensity' => 'Intensity',
			'fan' => 'Fan Percent',
			'engine' => 'GPU Clock',
			'mem' => 'Memory Clock',
			'vddc' => 'GPU Voltage' );

 $tb = '<tr><td><table border=1 cellpadding=5 cellspacing=0>';
 $te = '</table></td></tr>';

 echo $tb.'<tr>';

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
			echo "|$c\")'>";
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
			echo "<input type=button value='Set $c:' onclick='prs2(\"gpu$name|$c\",$n)'>";
			echo "<input size=7 type=text name=gi$n value='$value' id=gi$n>";
			$n++;
		}

		echo '</td>';
	}

 }

 echo '</tr>'.$te;
}
#
function processgpus($rd, $ro)
{
 global $error;

 $gpus = api('gpucount');

 if ($error != null)
	echo '<tr><td>Error getting GPU count: '.$rd.$error.$ro.'</td></tr>';
 else
 {
	if (!isset($gpus['GPUS']['Count']))
		echo '<tr><td>No GPU count returned: '.$rd.$gpus['STATUS']['STATUS'].' '.$gpus['STATUS']['Msg'].$ro.'</td></tr>';
	else
	{
		$count = $gpus['GPUS']['Count'];
		if ($count == 0)
			echo '<tr><td>No GPUs</td></tr>';
		else
			gpubuttons($count);
	}
 }
}
#
function process($cmds, $rd, $ro)
{
 global $error, $devs;

 foreach ($cmds as $cmd => $des)
 {
	$process = api($cmd);

	if ($error != null)
	{
		echo "<tr><td>Error getting $des: ";
		echo $rd.$error.$ro.'</td></tr>';
		break;
	}
	else
	{
		details($cmd, $process);
		echo '<tr><td><br><br></td></tr>';
		if ($cmd == 'devs')
			$devs = $process;
	}
 }
}
#
function display()
{
 global $error;

 $error = null;

 $rd = '<font color=red><b>';
 $ro = '</b></font>';

 echo "<tr><td><table cellpadding=0 cellspacing=0 border=0><tr><td>";
 echo "<input type=button value='Refresh' onclick='pr(\"\",null)'>";
 echo "</td><td width=100%>&nbsp;</td><td>";
 echo "<input type=button value='Quit' onclick='prc(\"quit\",\"Quit CGMiner\")'>";
 echo "</td></tr></table></td></tr>";

 $arg = trim(getparam('arg', true));
 if ($arg != null and $arg != '')
	process(array($arg => $arg), $rd, $ro);

 $cmds = array(	'devs'    => 'device list',
		'summary' => 'summary information',
		'pools'   => 'pool list',
		'config'  => 'cgminer config');

 process($cmds, $rd, $ro);

 if ($error == null)
	processgpus($rd, $ro);
}
#
display();
#
?>
</table></td></tr></table>
</body></html>
