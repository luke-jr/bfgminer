/*
 *
 * Copyright (C) Andrew Smith 2012-2013
 *
 * Usage: java API command ip port
 *
 * If any are missing or blank they use the defaults:
 *
 *	command = 'summary'
 *	ip	= '127.0.0.1'
 *	port	= '4028'
 *
 */

import java.net.*;
import java.io.*;

class API
{
	static private final int MAXRECEIVESIZE = 65535;

	static private Socket socket = null;

	private void closeAll() throws Exception
	{
		if (socket != null)
		{
			socket.close();
			socket = null;
		}
	}

	public void display(String result) throws Exception
	{
		String value;
		String name;
		String[] sections = result.split("\\|", 0);

		for (int i = 0; i < sections.length; i++)
		{
			if (sections[i].trim().length() > 0)
			{
				String[] data = sections[i].split(",", 0);

				for (int j = 0; j < data.length; j++)
				{
					String[] nameval = data[j].split("=", 2);

					if (j == 0)
					{
						if (nameval.length > 1
						&&  Character.isDigit(nameval[1].charAt(0)))
							name = nameval[0] + nameval[1];
						else
							name = nameval[0];

						System.out.println("[" + name + "] =>");
						System.out.println("(");
					}

					if (nameval.length > 1)
					{
						name = nameval[0];
						value = nameval[1];
					}
					else
					{
						name = "" + j;
						value = nameval[0];
					}

					System.out.println("   ["+name+"] => "+value);
				}
				System.out.println(")");
			}
		}
	}

	public void process(String cmd, InetAddress ip, int port) throws Exception
	{
		StringBuffer sb = new StringBuffer();
		char buf[] = new char[MAXRECEIVESIZE];
		int len = 0;

System.out.println("Attempting to send '"+cmd+"' to "+ip.getHostAddress()+":"+port);

		try
		{
			socket = new Socket(ip, port);
			PrintStream ps = new PrintStream(socket.getOutputStream());
			ps.print(cmd.toLowerCase().toCharArray());
			ps.flush();

			InputStreamReader isr = new InputStreamReader(socket.getInputStream());
			while (0x80085 > 0)
			{
				len = isr.read(buf, 0, MAXRECEIVESIZE);
				if (len < 1)
					break;
				sb.append(buf, 0, len);
				if (buf[len-1] == '\0')
					break;
			}

			closeAll();
		}
		catch (IOException ioe)
		{
			System.err.println(ioe.toString());
			closeAll();
			return;
		}

		String result = sb.toString();

		System.out.println("Answer='"+result+"'");

		display(result);
	}

	public API(String command, String _ip, String _port) throws Exception
	{
		InetAddress ip;
		int port;

		try
		{
			ip = InetAddress.getByName(_ip);
		}
		catch (UnknownHostException uhe)
		{
			System.err.println("Unknown host " + _ip + ": " + uhe);
			return;
		}

		try
		{
			port = Integer.parseInt(_port);
		}
		catch (NumberFormatException nfe)
		{
			System.err.println("Invalid port " + _port + ": " + nfe);
			return;
		}

		process(command, ip, port);
	}

	public static void main(String[] params) throws Exception
	{
		String command = "summary";
		String ip = "127.0.0.1";
		String port = "4028";

		if (params.length > 0 && params[0].trim().length() > 0)
			command = params[0].trim();

		if (params.length > 1 && params[1].trim().length() > 0)
			ip = params[1].trim();

		if (params.length > 2 && params[2].trim().length() > 0)
			port = params[2].trim();

		new API(command, ip, port);
	}
}
