<?php

$host = 'localhost';
$port = 11400;

$beanstalk = new Beanstalk();

try {
	$beanstalk->connect($host, $port);
}
catch (Exception $e) {
	echo $e->getMessage() . "\n";
}

$res = $beanstalk->useTube('foo');
var_dump($res);

//$res = $beanstalk->put(0, 0, 120, 'say hello world');
//var_dump($res);
$res = $beanstalk->put(0, 0, 120, 'say hello');
var_dump($res);

$beanstalk->close();

?>
