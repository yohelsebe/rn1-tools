#include <stdint.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <stropts.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <SFML/Graphics.hpp>

#ifndef M_PI
#define M_PI 3.14159265358
#endif

#define I14_I16(msb,lsb) ((int16_t)( ( ((uint16_t)(msb)<<9) | ((uint16_t)(lsb)<<2) ) ))

double mm_per_pixel = 10.0;

int screen_x = 1024;
int screen_y = 768;

double origin_x = ((double)screen_x/2.0)*mm_per_pixel;
double origin_y = ((double)screen_y/2.0)*mm_per_pixel;

double cur_x = 0.0;
double cur_y = 0.0;
double cur_angle = 5.0;

const double lidar_line_thick = 2.0;


int set_uart_attribs(int fd, int speed)
{
	struct termios tty;
	memset(&tty, 0, sizeof(tty));
	if(tcgetattr(fd, &tty) != 0)
	{
		printf("error %d from tcgetattr\n", errno);
		return -1;
	}

	cfmakeraw(&tty);
	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;

	tty.c_cflag |= (CLOCAL | CREAD);
	tty.c_cflag &= ~(PARENB | PARODD);
	tty.c_cflag &= ~CSTOPB;

	// nonblocking
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;

	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	if(tcsetattr(fd, TCSANOW, &tty) != 0)
	{
		printf("error %d from tcsetattr\n", errno);
		return -1;
	}
	return 0;
}

int uart;

void draw_robot(sf::RenderWindow& win)
{
	sf::ConvexShape r(5);
	r.setPoint(0, sf::Vector2f(0,0));
	r.setPoint(1, sf::Vector2f(0,12));
	r.setPoint(2, sf::Vector2f(15,12));
	r.setPoint(3, sf::Vector2f(20,6));
	r.setPoint(4, sf::Vector2f(15,0));

	r.setFillColor(sf::Color(200,70,50));
	r.setOrigin(10,6);

	r.setRotation(cur_angle);
	r.setPosition((cur_x+origin_x)/mm_per_pixel,(cur_y+origin_y)/mm_per_pixel);

	win.draw(r);
}

int lidar_scan[360];

void draw_lidar(sf::RenderWindow& win)
{
	for(int i = 0; i < 360; i++)
	{
		int ip = (i==359)?0:(i+1);
		int first = lidar_scan[i];
		int second = lidar_scan[ip];

		if(first == 0 && second == 0)
			continue;

		if(first == 0) first = second;
		else if(second == 0) second = first;

		if(abs(first-second) > 500)
			continue;

		double x1 = (cur_x+origin_x+cos(M_PI*(cur_angle+(double)i)/180.0) * first)/mm_per_pixel;
		double y1 = (cur_y+origin_y+sin(M_PI*(cur_angle+(double)i)/180.0) * first)/mm_per_pixel;
		double x2 = (cur_x+origin_x+cos(M_PI*(cur_angle+(double)ip)/180.0) * second)/mm_per_pixel;
		double y2 = (cur_y+origin_y+sin(M_PI*(cur_angle+(double)ip)/180.0) * second)/mm_per_pixel;
		sf::RectangleShape rect(sf::Vector2f( sqrt(pow(x2-x1,2)+pow(y2-y1,2)), lidar_line_thick));
		rect.setOrigin(0, lidar_line_thick/2.0);
		rect.setPosition(x1, y1);
		rect.setRotation(atan2(y2-y1,x2-x1)*180.0/M_PI);
		rect.setFillColor(sf::Color(220,30,30));

		win.draw(rect);
	}
}

sf::Font arial;

double gyro_x, gyro_y, gyro_z, xcel_x, xcel_y, xcel_z;

void draw_gyros(sf::RenderWindow& win)
{
	sf::Text t;
	t.setFont(arial);
	char buf[500];
	sprintf(buf, "Gyro x=%f  y=%f  z=%f", gyro_x, gyro_y, gyro_z);
	t.setString(buf);
	t.setCharacterSize(16);
	t.setColor(sf::Color(0,0,0));
	t.setPosition(10,10);
	win.draw(t);

	sprintf(buf, "Xcel x=%f  y=%f  z=%f", xcel_x, xcel_y, xcel_z);
	t.setString(buf);
	t.setCharacterSize(16);
	t.setColor(sf::Color(0,0,0));
	t.setPosition(10,32);
	win.draw(t);

}

int main(int argc, char** argv)
{
	uint8_t rxbuf[1024];
	uint8_t parsebuf[1024];
	int do_parse = 0;
	int rxloc = 0;

	if(argc != 2)
	{
		printf("Usage: visudrive /dev/ttyUSB0\n");
		return 1;
	}

	uart = open(argv[1], O_RDWR | O_NOCTTY);

	if(uart < 0)
	{
		printf("error %d opening %s: %s\n", errno, argv[1], strerror(errno));
		return 1;
	}

	set_uart_attribs(uart, B115200);

	if (!arial.loadFromFile("arial.ttf"))
	{
	    return 1;
	}

	sf::ContextSettings sets;
	sets.antialiasingLevel = 8;
	sf::RenderWindow win(sf::VideoMode(screen_x,screen_y), "RN#1 Visual Drive Hommeli Pommeli", sf::Style::Default, sets);

	for(int i = 0; i < 360; i++)
	{
		lidar_scan[i] = 1000+i;
	}

	lidar_scan[20] = 0;
	lidar_scan[50] = 0;
	lidar_scan[53] = 0;
	lidar_scan[56] = 0;

	lidar_scan[80] = 0;
	lidar_scan[81] = 0;

	lidar_scan[195] = 0;
	lidar_scan[196] = 0;
	lidar_scan[197] = 0;
	lidar_scan[198] = 0;
	lidar_scan[199] = 0;

	tcflush(uart, TCIFLUSH);

	while(win.isOpen())
	{
		sf::Event event;
		while (win.pollEvent(event))
		{
			if(event.type == sf::Event::Closed)
				win.close();
		}

		if(sf::Keyboard::isKeyPressed(sf::Keyboard::PageUp))
		{
			mm_per_pixel *= 1.1;
			origin_x = ((double)screen_x/2.0)*mm_per_pixel;
			origin_y = ((double)screen_y/2.0)*mm_per_pixel;
		}
		if(sf::Keyboard::isKeyPressed(sf::Keyboard::PageDown))
		{
			mm_per_pixel *= 0.9;
			origin_x = ((double)screen_x/2.0)*mm_per_pixel;
			origin_y = ((double)screen_y/2.0)*mm_per_pixel;
		}

		if(sf::Keyboard::isKeyPressed(sf::Keyboard::Left))
		{
			cur_angle -= 1.0;
		}
		if(sf::Keyboard::isKeyPressed(sf::Keyboard::Right))
		{
			cur_angle += 1.0;
		}
		if(sf::Keyboard::isKeyPressed(sf::Keyboard::Up))
		{
			cur_x += cos(M_PI*cur_angle/180.0);
			cur_y += sin(M_PI*cur_angle/180.0);
		}
		if(sf::Keyboard::isKeyPressed(sf::Keyboard::Down))
		{
			cur_x -= cos(M_PI*cur_angle/180.0);
			cur_y -= sin(M_PI*cur_angle/180.0);
		}

		win.clear(sf::Color(180,220,255));

		uint8_t byte;
		while(read(uart, &byte, 1))
		{
			if(rxloc > 1000)
				rxloc = 0;

			if(byte > 127)
			{
				memcpy(parsebuf, rxbuf, rxloc);
				do_parse = 1;
				rxbuf[0] = byte;
				rxloc = 1;
				break;
			}

			rxbuf[rxloc] = byte;
			rxloc++;
		}

		if(do_parse)
		{
			switch(parsebuf[0])
			{
				case 128:
				gyro_x = (double)I14_I16(parsebuf[3], parsebuf[2]);
				gyro_y = (double)I14_I16(parsebuf[5], parsebuf[4]);
				gyro_z = (double)I14_I16(parsebuf[7], parsebuf[6]);
				break;

				case 129:
				xcel_x = (double)I14_I16(parsebuf[3], parsebuf[2]);
				xcel_y = (double)I14_I16(parsebuf[5], parsebuf[4]);
				xcel_z = (double)I14_I16(parsebuf[7], parsebuf[6]);
				break;

				case 0x84:
				for(int i = 0; i < 360; i++)
				{
					lidar_scan[360-i] = parsebuf[2+2*i+1]<<7 | parsebuf[2+2*i];
				}
				break;

				default:
				break;
			}
			do_parse = 0;
		}

		draw_gyros(win);

		draw_robot(win);
		draw_lidar(win);

		win.display();
	}
	return 0;
}