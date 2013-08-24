/*
 *
 * Copyright (C) Andrew Smith 2013
 *
 * Usage: java MCast [-v] code toaddr port replyport wait
 *
 * If any are missing or blank they use the defaults:
 *
 *	-v means report how long the last reply took
 *
 *	code		= 'FTW'
 *	toaddr		= '224.0.0.75'
 *	port		= '4028'
 *	replyport	= '4027'
 *	wait		= '1000'
 *
 */

import java.net.*;
import java.io.*;
import java.util.*;

class MCast implements Runnable
{
	static private final String MCAST_CODE = "FTW";
	static private final String MCAST_ADDR = "224.0.0.75";
	static private final int MCAST_PORT = 4028;
	static private final int MCAST_REPORT = 4027;
	static private final int MCAST_WAIT4 = 1000;

	static private String code = MCAST_CODE;
	static private String addr = MCAST_ADDR;
	static private int port = MCAST_PORT;
	static private int report = MCAST_REPORT;
	static private int wait4 = MCAST_WAIT4;

	private InetAddress mcast_addr = null;

	static private final Integer lock = new Integer(666);

	static private boolean ready = false;

	static private Thread listen = null;

	static public boolean verbose = false;

	static private Date start = null;
	static private Date last = null;
	static boolean got_last = false;

	static public void usAge()
	{
		System.err.println("usAge: java MCast [-v] [code [toaddr [port [replyport [wait]]]]]");
		System.err.println("       -v=report elapsed ms to last reply");
		System.err.println("     Anything below missing or blank will use it's default");
		System.err.println("       code=X in cgminer-X-Port default="+MCAST_CODE);
		System.err.println("       toaddr=multicast address default="+MCAST_ADDR);
		System.err.println("       port=multicast port default="+MCAST_PORT);
		System.err.println("       replyport=local post to listen for replies default="+MCAST_REPORT);
		System.err.println("       wait=how long to wait for replies default="+MCAST_WAIT4+"ms");
		System.exit(1);
	}

	private int port(String _port, String name)
	{
		int tmp = 0;

		try
		{
			tmp = Integer.parseInt(_port);
		}
		catch (NumberFormatException nfe)
		{
			System.err.println("Invalid " + name + " - must be a number between 1 and 65535");
			usAge();
			System.exit(1);
		}

		if (tmp < 1 || tmp > 65535)
		{
			System.err.println("Invalid " + name + " - must be a number between 1 and 65535");
			usAge();
			System.exit(1);
		}

		return tmp;
	}

	public void set_code(String _code)
	{
		if (_code.length() > 0)
			code = _code;
	}

	public void set_addr(String _addr)
	{
		if (_addr.length() > 0)
		{
			addr = _addr;

			try
			{
				mcast_addr = InetAddress.getByName(addr);
			}
			catch (Exception e)
			{
				System.err.println("ERR: Invalid multicast address");
				usAge();
				System.exit(1);
			}
		}
	}

	public void set_port(String _port)
	{
		if (_port.length() > 0)
			port = port(_port, "port");
	}

	public void set_report(String _report)
	{
		if (_report.length() > 0)
			report = port(_report, "reply port");
	}

	public void set_wait(String _wait4)
	{
		if (_wait4.length() > 0)
		{
			try
			{
				wait4 = Integer.parseInt(_wait4);
			}
			catch (NumberFormatException nfe)
			{
				System.err.println("Invalid wait - must be a number between 0ms and 60000ms");
				usAge();
				System.exit(1);
			}

			if (wait4 < 0 || wait4 > 60000)
			{
				System.err.println("Invalid wait - must be a number between 0ms and 60000ms");
				usAge();
				System.exit(1);
			}
		}
	}

	public void run() // listen
	{
		byte[] message = new byte[1024];
		DatagramSocket socket = null;
		DatagramPacket packet = null;

		try
		{
			socket = new DatagramSocket(report);
			packet = new DatagramPacket(message, message.length);

			synchronized (lock)
			{
				ready = true;
			}

			while (true)
			{
				socket.receive(packet);

				synchronized (lock)
				{
					last = new Date();
				}

				int off = packet.getOffset();
				int len = packet.getLength();

				System.out.println("Got: '" + new String(message, off, len) + "' from" + packet.getSocketAddress());
			}
		}
		catch (Exception e)
		{
			socket.close();
		}
	}

	public void sendMCast()
	{
		try
		{
			String message = new String("cgminer-" + code + "-" + report);
			MulticastSocket socket = null;
			DatagramPacket packet = null;

			socket = new MulticastSocket();
			packet = new DatagramPacket(message.getBytes(), message.length(), mcast_addr, port);

			System.out.println("About to send " + message + " to " + mcast_addr + ":" + port);

			start = new Date();

			socket.send(packet);

			socket.close();
		}
		catch (Exception e)
		{
			e.printStackTrace();
		}
	}

	public void init()
	{
		MCast lis = new MCast();
		listen = new Thread(lis);
		listen.start();

		while (true)
		{
			synchronized (lock)
			{
				if (ready)
					break;
			}

			try
			{
				Thread.sleep(100);
			}
			catch (Exception sl1)
			{
			}
		}

		try
		{
			Thread.sleep(500);
		}
		catch (Exception sl2)
		{
		}

		sendMCast();

		try
		{
			Thread.sleep(wait4);
		}
		catch (Exception sl3)
		{
		}

		listen.interrupt();

		if (verbose)
		{
			try
			{
				Thread.sleep(100);
			}
			catch (Exception sl4)
			{
			}

			synchronized (lock)
			{
				if (last == null)
					System.out.println("No replies received");
				else
				{
					long diff = last.getTime() - start.getTime();
					System.out.println("Last reply took " + diff + "ms");
				}
			}
		}

		System.exit(0);
	}

	public MCast()
	{
	}

	public static void main(String[] params) throws Exception
	{
		int p = 0;

		MCast mcast = new MCast();

		mcast.set_addr(MCAST_ADDR);

		if (params.length > p)
		{
			if (params[p].equals("-?")
			||  params[p].equalsIgnoreCase("-h")
			||  params[p].equalsIgnoreCase("-help")
			||  params[p].equalsIgnoreCase("--help"))
				MCast.usAge();
			else
			{
				if (params[p].equals("-v"))
				{
					mcast.verbose = true;
					p++;
				}

				if (params.length > p)
				{
					mcast.set_code(params[p++]);

					if (params.length > p)
					{
						mcast.set_addr(params[p++]);

						if (params.length > p)
						{
							mcast.set_port(params[p++]);

							if (params.length > p)
							{
								mcast.set_report(params[p++]);
								if (params.length > p)
									mcast.set_wait(params[p]);
							}
						}
					}
				}
			}
		}

		mcast.init();
	}
}
