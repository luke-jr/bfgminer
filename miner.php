<?php
session_start();
#
global $miner, $port, $readonly, $notify, $rigs;
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
# Set $rigs to an array of your cgminer rigs that are running
#  format: 'IP:Port' or 'Host:Port'
# If you only have one rig, it will just show the detail of that rig
# If you have more than one rig it will show a summary of all the rigs
#  with buttons to show the details of each rig
# e.g. $rigs = array('127.0.0.1:4028','myrig.com:4028');
$rigs = array('127.0.0.1:4028');
#
$here = $_SERVER['PHP_SELF'];
#
global $tablebegin, $tableend, $warnfont, $warnoff;
$tablebegin = '<tr><td><table border=1 cellpadding=5 cellspacing=0>';
$tableend = '</table></td></tr>';
$warnfont = '<font color=red><b>';
$warnoff = '</b></font>';

#
function htmlhead($checkapi)
{
 global $error, $readonly, $here;
 if ($readonly === false && $checkapi === true)
 {
	$access = api('privileged');
	if ($error != null
	||  !isset($access['STATUS']['STATUS'])
	||  $access['STATUS']['STATUS'] != 'S')
		$readonly = true;
 }
?>
<html><head><title>Mine</title>
<style type='text/css'>
td { color:blue; font-family:verdana,arial,sans; font-size:13pt; }
td.h { color:blue; font-family:verdana,arial,sans; font-size:13pt; background:#d0ffff }
td.err { color:black; font-family:verdana,arial,sans; font-size:13pt; background:#ff3050 }
td.warn { color:black; font-family:verdana,arial,sans; font-size:13pt; background:#ffb050 }
td.sta { color:green; font-family:verdana,arial,sans; font-size:13pt; }
td.tot { color:blue; font-family:verdana,arial,sans; font-size:13pt; background:#fff8f2 }
</style>
</head><body bgcolor=#ecffff>
<script type='text/javascript'>
function pr(a,m){if(m!=null){if(!confirm(m+'?'))return}window.location="<?php echo $here ?>"+a}
<?php
 if ($readonly === false && $checkapi === true)
 {
?>
function prc(a,m){pr('?arg='+a,m)}
function prs(a,r){var c=a.substr(3);var z=c.split('|',2);var m=z[0].substr(0,1).toUpperCase()+z[0].substr(1)+' GPU '+z[1];prc(a+'&rig='+r,m)}
function prs2(a,n,r){var v=document.getElementById('gi'+n).value;var c=a.substr(3);var z=c.split('|',2);var m='Set GPU '+z[1]+' '+z[0].substr(0,1).toUpperCase()+z[0].substr(1)+' to '+v;prc(a+','+v+'&rig='+r,m)}
<?php
 }
?>
</script>
<table width=100% height=100% border=0 cellpadding=0 cellspacing=0 summary='Mine'>
<tr><td align=center valign=top>
<table border=0 cellpadding=4 cellspacing=0 summary='Mine'>
<?php
}
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
 $errorclass = ' class=err';
 $warnclass = ' class=warn';
 $b = '&nbsp;';

 $ret = $value;
 $class = '';

 switch ($section.'.'.$name)
 {
 case 'GPU.Last Share Time':
 case 'PGA.Last Share Time':
	$ret = date('H:i:s', $value);
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
	$ret = $value.'/m';
	break;
 case 'GPU.Temperature':
 case 'PGA.Temperature':
	$ret = $value.'&deg;C';
	break;
 case 'GPU.MHS av':
 case 'PGA.MHS av':
 case 'SUMMARY.MHS av':
 case 'GPU.Total MH':
 case 'PGA.Total MH':
 case 'SUMMARY.Total MH':
 case 'SUMMARY.Getworks':
 case 'GPU.Accepted':
 case 'PGA.Accepted':
 case 'SUMMARY.Accepted':
 case 'GPU.Rejected':
 case 'PGA.Rejected':
 case 'SUMMARY.Rejected':
 case 'SUMMARY.Local Work':
 case 'POOL.Getworks':
 case 'POOL.Accepted':
 case 'POOL.Rejected':
 case 'POOL.Discarded':
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
 }

 if ($section == 'NOTIFY' && substr($name, 0, 1) == '*' && $value != '0')
	$class = $errorclass;

 return array($ret, $class);
}
#
global $poolcmd;
$poolcmd = array(	'Switch to'	=> 'switchpool',
			'Enable'	=> 'enablepool',
			'Disable'	=> 'disablepool' );
#
function showhead($cmd, $item, $values)
{
 global $poolcmd, $readonly;

 echo '<tr>';

 foreach ($values as $name => $value)
 {
	if ($name == '0')
		$name = '&nbsp;';
	echo "<td valign=bottom class=h>$name</td>";
 }

 if ($cmd == 'pools' && $readonly === false)
	foreach ($poolcmd as $name => $pcmd)
		echo "<td valign=bottom class=h>$name</td>";

 echo '</tr>';
}
#
function details($cmd, $list, $rig)
{
 global $tablebegin, $tableend;
 global $poolcmd, $readonly;

 $dfmt = 'H:i:s j-M-Y \U\T\CP';

 $stas = array('S' => 'Success', 'W' => 'Warning', 'I' => 'Informational', 'E' => 'Error', 'F' => 'Fatal');

 echo $tablebegin;

 echo '<tr><td class=sta>Date: '.date($dfmt).'</td></tr>';

 echo $tableend.$tablebegin;

 if (isset($list['STATUS']))
 {
	echo '<tr>';
	echo '<td>Computer: '.$list['STATUS']['Description'].'</td>';
	if (isset($list['STATUS']['When']))
		echo '<td>When: '.date($dfmt, $list['STATUS']['When']).'</td>';
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
		showhead($cmd, $item, $values);
		$section = $sectionname;
	}

	echo '<tr>';

	foreach ($values as $name => $value)
	{
		list($showvalue, $class) = fmt($section, $name, $value);
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
		echo '<tr><td><br><br></td></tr>';
		if ($cmd == 'devs')
			$devs = $process;
	}
 }
}
#
# $head is a hack but this is just a demo anyway :)
function doforeach($cmd, $des, $sum, $head)
{
 global $miner, $port;
 global $error, $readonly, $notify, $rigs;
 global $tablebegin, $tableend, $warnfont, $warnoff;

 $header = $head;
 $anss = array();

 $count = 0;
 foreach ($rigs as $rig)
 {
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
			$error = null;
		}
		else
			$anss[$count] = $ans;
	}
	$count++;
 }

 if (count($anss) == 0)
 {
	echo "<tr><td>Failed to access any rigs successfully</td></tr>";
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

 showhead('', null, $header);

 $section = '';

 foreach ($anss as $rig => $ans)
 {
	foreach ($ans as $item => $row)
	{
		if ($item == 'STATUS')
			continue;

		echo '<tr>';

		$newsection = preg_replace('/\d/', '', $item);
		if ($newsection != 'total')
			$section = $newsection;

		foreach ($header as $name => $x)
		{
			if ($name == '')
			{
				if ($rig === 'total')
					echo "<td align=right class=tot>Total:</td>";
				else
					echo "<td align=right><input type=button value='Rig $rig' onclick='pr(\"?rig=$rig\",null)'></td>";
			}
			else
			{
				if (isset($row[$name]))
					list($showvalue, $class) = fmt($section, $name, $row[$name]);
				else
				{
					$class = '';
					$showvalue = '&nbsp;';
				}

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
function doOne($rig, $preprocess)
{
 global $error, $readonly, $notify;
 global $rigs;

 htmlhead(true);

 $error = null;

 echo "<tr><td><table cellpadding=0 cellspacing=0 border=0><tr><td>";
 echo "<input type=button value='Refresh' onclick='pr(\"?rig=$rig\",null)'></td>";
 if (count($rigs) > 1)
	echo "<td><input type=button value='Summary' onclick='pr(\"\",null)'></td>";
 echo "<td width=100%>&nbsp;</td><td>";
 if ($readonly === false)
 {
	$msg = 'Quit CGMiner';
	if (count($rigs) > 1)
		$msg .= " Rig $rig";
	echo "<input type=button value='Quit' onclick='prc(\"quit&rig=$rig\",\"$msg\")'>";
 }
 echo "</td></tr></table></td></tr>";

 if ($preprocess != null)
	process(array($preprocess => $preprocess), $rig);

 $cmds = array(	'devs'    => 'device list',
		'summary' => 'summary information',
		'pools'   => 'pool list');

 if ($notify)
	$cmds['notify'] = 'device status';

 $cmds['config'] = 'cgminer config';

 process($cmds, $rig);

 if ($error == null && $readonly === false)
	processgpus($rig);
}
#
function display()
{
 global $tablebegin, $tableend;
 global $miner, $port;
 global $error, $readonly, $notify, $rigs;

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

 htmlhead(false);

 echo "<tr><td><table cellpadding=0 cellspacing=0 border=0><tr><td>";
 echo "<input type=button value='Refresh' onclick='pr(\"\",null)'>";
 echo "</td></tr></table></td></tr>";

 if ($preprocess != null)
	process(array($preprocess => $preprocess), $rig);

 echo $tablebegin;
 $sum = array('MHS av', 'Getworks', 'Found Blocks', 'Accepted', 'Rejected', 'Discarded', 'Stale', 'Utility', 'Local Work', 'Total MH');
 doforeach('summary', 'summary information', $sum, array());
 echo $tableend;
 echo '<tr><td><br><br></td></tr>';
 echo $tablebegin;
 doforeach('devs', 'device list', $sum, array(''=>'','ID'=>'','Name'=>''));
 echo $tableend;
 echo '<tr><td><br><br></td></tr>';
 echo $tablebegin;
 doforeach('pools', 'pool list', $sum, array(''=>''));
 echo $tableend;
}
#
display();
#
?>
</table></td></tr></table>
</body></html>
