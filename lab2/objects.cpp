/*********************************************************************
	Simulation obiekt�w fizycznych ruchomych np. samochody, statki, roboty, itd.
	+ obs�uga obiekt�w statycznych np. env.
	**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include <gl\gl.h>
#include <gl\glu.h>
#include "objects.h"
#include "graphics.h"

extern FILE *f;
extern Environment env;

extern bool if_ID_visible;


MovableObject::MovableObject()             // konstruktor                   
{
	iID = (unsigned int)(rand() % 1000);  // identyfikator obiektu
	fprintf(f, "my_car->iID = %d\n", iID);

	// zmienne zwi�zame z akcjami kierowcy
	F = Fb = 0;	// si�y dzia�aj�ce na obiekt 
	breaking_factor = 0;			// stopie� hamowania
	steer_wheel_speed = 0;  // pr�dko�� kr�cenia kierownic� w rad/s
	if_keep_steer_wheel = 0;  // informacja czy kieronica jest trzymana

	// sta�e samochodu
	mass_own = 6.0;			// masa obiektu [kg]
	//Fy = mass_own*9.81;        // si�a nacisku na podstaw� obiektu (na ko�a pojazdu)
	length = 9.0;
	width = 5.0;
	height = 1.7;
	clearance = 0.0;     // wysoko�� na kt�rej znajduje si� podstawa obiektu
	front_axis_dist = 1.0;     // odleg�o�� od przedniej osi do przedniego zderzaka 
	back_axis_dist = 0.2;       // odleg�o�� od tylniej osi do tylniego zderzaka
	steer_wheel_ret_speed = 4.0; // pr�dko�� powrotu kierownicy w rad/s

	// parametry stateu auta:
	state.steering_angle = 0;
	state.vPos.y = clearance + height / 2 + 20; // wysoko�� �rodka ci�ko�ci w osi pionowej pojazdu
	state.vPos.x = 0;
	state.vPos.z = 0;
	quaternion qObr = AsixToQuat(Vector3(0, 1, 0), 0.1*PI / 180.0); // obr�t obiektu o k�t 30 stopni wzgl�dem osi y:
	state.qOrient = qObr*state.qOrient;
}

MovableObject::~MovableObject()            // destruktor
{
}

void MovableObject::ChangeState(ObjectState __state)  // przepisanie podanego stateu 
{                                                // w przypadku obiekt�w, kt�re nie s� symulowane
	state = __state;
}

ObjectState MovableObject::State()                // metoda zwracaj�ca state obiektu ��cznie z iID
{
	return state;
}



void MovableObject::Simulation(float dt)          // obliczenie nowego stateu na podstawie dotychczasowego,
{                                                // dzia�aj�cych si� i czasu, jaki up�yn�� od ostatniej symulacji

	if (dt == 0) return;

	float friction = 5.0;            // wsp�czynnik tarcia obiektu o pod�o�e
	float friction_rot = friction;
	float friction_roll = 0.05;    // wsp�czynnik tarcia tocznego
	float elasticity = 0.5;       // wsp�czynnik spr�ysto�ci (0-brak spr�ysto�ci, 1-doskona�a spr�ysto��) 
	float g = 9.81;                // przyspieszenie grawitacyjne
	float Fy = mass_own*9.81;        // si�a nacisku na podstaw� obiektu (na ko�a pojazdu)

	// obracam uk�ad wsp�rz�dnych lokalnych wed�ug quaterniona orientacji:
	Vector3 dir_forward = state.qOrient.rotate_vector(Vector3(1, 0, 0)); // na razie o� obiektu pokrywa si� z osi� x globalnego uk�adu wsp�rz�dnych (lokalna o� x)
	Vector3 dir_up = state.qOrient.rotate_vector(Vector3(0, 1, 0));  // wektor skierowany pionowo w g�r� od podstawy obiektu (lokalna o� y)
	Vector3 dir_right = state.qOrient.rotate_vector(Vector3(0, 0, 1)); // wektor skierowany w prawo (lokalna o� z)


	// rzutujemy vV na sk�adow� w kierunku przodu i pozosta�e 2 sk�adowe
	// sk�adowa w bok jest zmniejszana przez si�� tarcia, sk�adowa do przodu
	// przez si�� tarcia tocznego
	Vector3 vV_forward = dir_forward*(state.vV^dir_forward),
		vV_right = dir_right*(state.vV^dir_right),
		vV_up = dir_up*(state.vV^dir_up);

	// rzutujemy pr�dko�� k�tow� vV_ang na sk�adow� w kierunku przodu i pozosta�e 2 sk�adowe
	Vector3 vV_ang_forward = dir_forward*(state.vV_ang^dir_forward),
		vV_ang_right = dir_right*(state.vV_ang^dir_right),
		vV_ang_up = dir_up*(state.vV_ang^dir_up);


	// ruch k� na skutek kr�cenia lub puszczenia kierownicy:  

	if (steer_wheel_speed != 0)
		state.steering_angle += steer_wheel_speed*dt;
	else
		if (state.steering_angle > 0)
		{
			if (!if_keep_steer_wheel)
				state.steering_angle -= steer_wheel_ret_speed*dt;
			if (state.steering_angle < 0) state.steering_angle = 0;
		}
		else if (state.steering_angle < 0)
		{
			if (!if_keep_steer_wheel)
				state.steering_angle += steer_wheel_ret_speed*dt;
			if (state.steering_angle > 0) state.steering_angle = 0;
		}
	// ograniczenia: 
	if (state.steering_angle > PI*60.0 / 180) state.steering_angle = PI*60.0 / 180;
	if (state.steering_angle < -PI*60.0 / 180) state.steering_angle = -PI*60.0 / 180;

	// obliczam promien skr�tu pojazdu na podstawie k�ta skr�tu k�, a nast�pnie na podstawie promienia skr�tu
	// obliczam pr�dko�� k�tow� (UPROSZCZENIE! pomijam przyspieszenie k�towe oraz w�a�ciw� trajektori� ruchu)
	if (Fy > 0)
	{
		float V_ang_turn = 0;
		if (state.steering_angle != 0)
		{
			float Rs = sqrt(length*length / 4 + (fabs(length / tan(state.steering_angle)) + width / 2)*(fabs(length / tan(state.steering_angle)) + width / 2));
			V_ang_turn = vV_forward.length()*(1.0 / Rs);
		}
		Vector3 vV_ang_turn = dir_up*V_ang_turn*(state.steering_angle > 0 ? 1 : -1);
		Vector3 vV_ang_up2 = vV_ang_up + vV_ang_turn;
		if (vV_ang_up2.length() <= vV_ang_up.length()) // skr�t przeciwdzia�a obrotowi
		{
			if (vV_ang_up2.length() > V_ang_turn)
				vV_ang_up = vV_ang_up2;
			else
				vV_ang_up = vV_ang_turn;
		}
		else
		{
			if (vV_ang_up.length() < V_ang_turn)
				vV_ang_up = vV_ang_turn;
		}

		// friction zmniejsza pr�dko�� obrotow� (UPROSZCZENIE! zamiast masy winienem wykorzysta� moment bezw�adno�ci)     
		float V_ang_friction = Fy*friction_rot*dt / mass_own / 1.0;      // zmiana pr. k�towej spowodowana frictionm
		float V_ang_up = vV_ang_up.length() - V_ang_friction;
		if (V_ang_up < V_ang_turn) V_ang_up = V_ang_turn;        // friction nie mo�e spowodowa� zmiany zwrotu wektora pr. k�towej
		vV_ang_up = vV_ang_up.znorm()*V_ang_up;
	}


	Fy = mass_own*g*dir_up.y;                      // si�a docisku do pod�o�a 
	if (Fy < 0) Fy = 0;
	// ... trzeba j� jeszcze uzale�ni� od tego, czy obiekt styka si� z pod�o�em!
	float Fh = Fy*friction*breaking_factor;                  // si�a hamowania (UP: bez uwzgl�dnienia po�lizgu)

	float V_up = vV_forward.length();// - dt*Fh/m - dt*friction_roll*Fy/m;
	if (V_up < 0) V_up = 0;

	float V_right = vV_right.length();// - dt*friction*Fy/m;
	if (V_right < 0) V_right = 0;


	// wjazd lub zjazd: 
	//vPos.y = env.DistFromGround(vPos.x,vPos.z);   // najprostsze rozwi�zanie - obiekt zmienia wysoko�� bez zmiany orientacji

	// 1. gdy wjazd na wkl�s�o��: wyznaczam wysoko�ci envu pod naro�nikami obiektu (ko�ami), 
	// sprawdzam kt�ra tr�jka
	// naro�nik�w odpowiada najni�ej po�o�onemu �rodkowi ci�ko�ci, gdy przylega do envu
	// wyznaczam pr�dko�� podbicia (wznoszenia �rodka pojazdu spowodowanej wkl�s�o�ci�) 
	// oraz pr�dko�� k�tow�
	// 2. gdy wjazd na wypuk�o�� to si�a ci�ko�ci wywo�uje obr�t przy du�ej pr�dko�ci liniowej

	// punkty zaczepienia k� (na wysoko�ci pod�ogi pojazdu):
	Vector3 P = state.vPos + dir_forward*(length / 2 - front_axis_dist) - dir_right*width / 2 - dir_up*height / 2,
		Q = state.vPos + dir_forward*(length / 2 - front_axis_dist) + dir_right*width / 2 - dir_up*height / 2,
		R = state.vPos + dir_forward*(-length / 2 + back_axis_dist) - dir_right*width / 2 - dir_up*height / 2,
		S = state.vPos + dir_forward*(-length / 2 + back_axis_dist) + dir_right*width / 2 - dir_up*height / 2;

	// pionowe rzuty punkt�w zacz. k� pojazdu na powierzchni� envu:  
	Vector3 Pt = P, Qt = Q, Rt = R, St = S;
	Pt.y = env.DistFromGround(P.x, P.z); Qt.y = env.DistFromGround(Q.x, Q.z);
	Rt.y = env.DistFromGround(R.x, R.z); St.y = env.DistFromGround(S.x, S.z);
	Vector3 normPQR = normal_vector(Pt, Rt, Qt), normPRS = normal_vector(Pt, Rt, St), normPQS = normal_vector(Pt, St, Qt),
		normQRS = normal_vector(Qt, Rt, St);   // normalne do p�aszczyzn wyznaczonych przez tr�jk�ty

	//fprintf(f, "P.y = %f, Pt.y = %f, Q.y = %f, Qt.y = %f, R.y = %f, Rt.y = %f, S.y = %f, St.y = %f\n",
	//	P.y, Pt.y, Q.y, Qt.y, R.y, Rt.y, S.y, St.y);

	float sryPQR = ((Qt^normPQR) - normPQR.x*state.vPos.x - normPQR.z*state.vPos.z) / normPQR.y, // wys. �rodka pojazdu
		sryPRS = ((Pt^normPRS) - normPRS.x*state.vPos.x - normPRS.z*state.vPos.z) / normPRS.y, // po najechaniu na skarp� 
		sryPQS = ((Pt^normPQS) - normPQS.x*state.vPos.x - normPQS.z*state.vPos.z) / normPQS.y, // dla 4 tr�jek k�
		sryQRS = ((Qt^normQRS) - normQRS.x*state.vPos.x - normQRS.z*state.vPos.z) / normQRS.y;
	float sry = sryPQR; Vector3 norm = normPQR;
	if (sry > sryPRS) { sry = sryPRS; norm = normPRS; }
	if (sry > sryPQS) { sry = sryPQS; norm = normPQS; }
	if (sry > sryQRS) { sry = sryQRS; norm = normQRS; }  // wyb�r tr�jk�ta o �rodku najni�ej po�o�onym    

	Vector3 vV_ang_horizontal = Vector3(0, 0, 0);
	// jesli kt�re� z k� jest poni�ej powierzchni envu
	if ((P.y <= Pt.y + height / 2 + clearance) || (Q.y <= Qt.y + height / 2 + clearance) ||
		(R.y <= Rt.y + height / 2 + clearance) || (S.y <= St.y + height / 2 + clearance))
	{
		// obliczam powsta�� pr�dko�� k�tow� w lokalnym uk�adzie wsp�rz�dnych:      
		Vector3 v_rotation = -norm.znorm()*dir_up*0.6;
		vV_ang_horizontal = v_rotation / dt;
	}

	Vector3 vAg = Vector3(0, -1, 0)*g;    // przyspieszenie grawitacyjne

	// jesli wiecej niz 2 kola sa na ziemi, to przyspieszenie grawitacyjne jest rownowazone przez opor gruntu:
	if ((P.y <= Pt.y + height / 2 + clearance) + (Q.y <= Qt.y + height / 2 + clearance) +
		(R.y <= Rt.y + height / 2 + clearance) + (S.y <= St.y + height / 2 + clearance) > 2)
		vAg = vAg + dir_up*(dir_up^vAg)*-1; //przyspieszenie resultaj�ce z si�y oporu gruntu
	else   // w przeciwnym wypadku brak sily docisku 
		Fy = 0;


	// sk�adam z powrotem wektor pr�dko�ci k�towej: 
	//state.vV_ang = vV_ang_up + vV_ang_right + vV_ang_forward;  
	state.vV_ang = vV_ang_up + vV_ang_horizontal;


	float h = sry + height / 2 + clearance - state.vPos.y;  // r�nica wysoko�ci jak� trzeba pokona�  
	float V_podbicia = 0;
	if ((h > 0) && (state.vV.y <= 0.01))
		V_podbicia = 0.5*sqrt(2 * g*h);  // pr�dko�� spowodowana podbiciem pojazdu przy wje�d�aniu na skarp� 
	if (h > 0) state.vPos.y = sry + height / 2 + clearance;

	// lub  w przypadku zag��bienia si� 
	Vector3 dvPos = state.vV*dt + state.vA*dt*dt / 2; // czynnik bardzo ma�y - im wi�ksza cz�stotliwo�� symulacji, tym mniejsze znaczenie 
	state.vPos = state.vPos + dvPos;

	// korekta po�o�enia w przypadku envu cyklicznego:
	if (state.vPos.x < -env.field_size*env.number_of_columns / 2) state.vPos.x += env.field_size*env.number_of_columns;
	else if (state.vPos.x > env.field_size*(env.number_of_columns - env.number_of_columns / 2)) state.vPos.x -= env.field_size*env.number_of_columns;
	if (state.vPos.z < -env.field_size*env.number_of_rows / 2) state.vPos.z += env.field_size*env.number_of_rows;
	else if (state.vPos.z > env.field_size*(env.number_of_rows - env.number_of_rows / 2)) state.vPos.z -= env.field_size*env.number_of_rows;

	// Sprawdzenie czy obiekt mo�e si� przemie�ci� w zadane miejsce: Je�li nie, to 
	// przemieszczam obiekt do miejsca zetkni�cia, wyznaczam nowe wektory pr�dko�ci
	// i pr�dko�ci k�towej, a nast�pne obliczam nowe po�o�enie na podstawie nowych
	// pr�dko�ci i pozosta�ego czasu. Wszystko powtarzam w p�tli (pojazd znowu mo�e 
	// wjecha� na przeszkod�). Problem z zaokr�glonymi przeszkodami - konieczne 
	// wyznaczenie minimalnego kroku.


	Vector3 vV_pop = state.vV;

	// sk�adam pr�dko�ci w r�nych kierunkach oraz efekt przyspieszenia w jeden wektor:    (problem z przyspieszeniem od si�y tarcia -> to przyspieszenie 
	//      mo�e dzia�a� kr�cej ni� dt -> trzeba to jako� uwzgl�dni�, inaczej pojazd b�dzie w�ykowa�)
	state.vV = vV_forward.znorm()*V_up + vV_right.znorm()*V_right + vV_up +
		Vector3(0, 1, 0)*V_podbicia + state.vA*dt;
	// usuwam te sk�adowe wektora pr�dko�ci w kt�rych kierunku jazda nie jest mo�liwa z powodu
	// przesk�d:
	// np. je�li pojazd styka si� 3 ko�ami z nawierzchni� lub dwoma ko�ami i �rodkiem ci�ko�ci to
	// nie mo�e mie� pr�dko�ci w d� pod�ogi
	if ((P.y <= Pt.y + height / 2 + clearance) || (Q.y <= Qt.y + height / 2 + clearance) ||
		(R.y <= Rt.y + height / 2 + clearance) || (S.y <= St.y + height / 2 + clearance))    // je�li pojazd styka si� co najm. jednym ko�em
	{
		Vector3 dvV = vV_up + dir_up*(state.vA^dir_up)*dt;
		if ((dir_up.znorm() - dvV.znorm()).length() > 1)  // je�li wektor skierowany w d� pod�ogi
			state.vV = state.vV - dvV;
	}

	// sk�adam przyspieszenia liniowe od si� nap�dzaj�cych i od si� oporu: 
	state.vA = (dir_forward*F + dir_right*Fb) / mass_own*(Fy > 0)  // od si� nap�dzaj�cych
		- vV_forward.znorm()*(Fh / mass_own + friction_roll*Fy / mass_own)*(V_up > 0.01) // od hamowania i tarcia tocznego (w kierunku ruchu)
		- vV_right.znorm()*friction*Fy / mass_own*(V_right > 0.01)    // od tarcia w kierunku prost. do kier. ruchu
		+ vAg;           // od grawitacji


	// obliczenie nowej orientacji:
	Vector3 w_obrot = state.vV_ang*dt + state.vA_ang*dt*dt / 2;
	quaternion q_obrot = AsixToQuat(w_obrot.znorm(), w_obrot.length());
	state.qOrient = q_obrot*state.qOrient;
}

void MovableObject::DrawObject()
{
	glPushMatrix();


	glTranslatef(state.vPos.x, state.vPos.y + clearance, state.vPos.z);

	Vector3 k = state.qOrient.AsixAngle();     // reprezentacja k�towo-osiowa quaterniona

	Vector3 k_znorm = k.znorm();

	glRotatef(k.length()*180.0 / PI, k_znorm.x, k_znorm.y, k_znorm.z);
	glTranslatef(-length / 2, -height / 2, -width / 2);
	glScalef(length, height, width);

	glCallList(Auto);
	GLfloat Surface[] = { 2.0f, 2.0f, 1.0f, 1.0f };
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, Surface);
	if (if_ID_visible){
		glRasterPos2f(0.30, 1.20);
		glPrint("%d", iID);
	}
	glPopMatrix();
}




//**********************
//   Obiekty nieruchome
//**********************
Environment::Environment()
{
   field_size = 35;         // d�ugo�� boku kwadratu w [m]           

   int wynik = ReadMap("map.txt");
   if (wynik == 0)
	   wynik = ReadMap("..//map.txt");
   if (wynik == 0)
	   fprintf(f, "Cannot open map.txt file. Check if this file exists in project directory!\n");


   d = new float**[number_of_rows];
   for (long i=0;i<number_of_rows;i++) {
       d[i] = new float*[number_of_columns];
       for (long j=0;j<number_of_columns;j++) d[i][j] = new float[4];
   }    
   Norm = new Vector3**[number_of_rows];
   for (long i=0;i<number_of_rows;i++) {
       Norm[i] = new Vector3*[number_of_columns];
       for (long j=0;j<number_of_columns;j++) Norm[i][j] = new Vector3[4];
   }    
       
   fprintf(f,"height_map env: number_of_rows = %d, number_of_columns = %d\n",number_of_rows,number_of_columns);
}

Environment::~Environment()
{
  for (long i = 0;i< number_of_rows*2+1;i++) delete height_map[i];             
  delete height_map;   
  for (long i=0;i<number_of_rows;i++)  {
      for (long j=0;j<number_of_columns;j++) delete d[i][j];
      delete d[i];
  }
  delete d;  
  for (long i=0;i<number_of_rows;i++)  {
      for (long j=0;j<number_of_columns;j++) delete Norm[i][j];
      delete Norm[i];
  }
  delete Norm;  

         
}

float Environment::DistFromGround(float x,float z)      // okre�lanie wysoko�ci dla punktu o wsp. (x,z) 
{
  
  float x_begin = -field_size*number_of_columns/2,     // wsp�rz�dne lewego g�rnego kra�ca envu
        z_begin = -field_size*number_of_rows/2;        
  
  long k = (long)((x - x_begin)/field_size), // wyznaczenie wsp�rz�dnych (w,k) kwadratu
       w = (long)((z - z_begin)/field_size);
  //if ((k < 0)||(k >= number_of_rows)||(w < 0)||(w >= number_of_columns)) return -1e10;  // je�li poza map�

  // korekta numeru kolumny lub wiersza w przypadku envu cyklicznego
  if (k<0) while (k<0) k += number_of_columns;
  else if (k > number_of_columns - 1) while (k > number_of_columns - 1) k -= number_of_columns;
  if (w<0) while (w<0) w += number_of_rows;
  else if (w > number_of_rows - 1) while (w > number_of_rows - 1) w -= number_of_rows;
  
  // wyznaczam punkt B - �rodek kwadratu oraz tr�jk�t, w kt�rym znajduje si� punkt
  // (rysunek w Environment::DrawInitialisation())
  Vector3 B = Vector3(x_begin + (k+0.5)*field_size, height_map[w*2+1][k], z_begin + (w+0.5)*field_size); 
  enum tr{ABC=0,ADB=1,BDE=2,CBE=3};       // tr�jk�t w kt�rym znajduje si� punkt 
  int triangle=0; 
  if ((B.x > x)&&(fabs(B.z - z) < fabs(B.x - x))) triangle = ADB;
  else if ((B.x < x)&&(fabs(B.z - z) < fabs(B.x - x))) triangle = CBE;
  else if ((B.z > z)&&(fabs(B.z - z) > fabs(B.x - x))) triangle = ABC;
  else triangle = BDE;
  
  // wyznaczam normaln� do p�aszczyzny a nast�pnie wsp�czynnik d z r�wnania p�aszczyzny
  float dd = d[w][k][triangle];
  Vector3 N = Norm[w][k][triangle];
  float y;
  if (N.y > 0) y = (-dd - N.x*x - N.z*z)/N.y;
  else y = 0;
  
  return y;    
}

void Environment::DrawInitialisation()
{
  // tworze list� wy�wietlania rysuj�c poszczeg�lne pola mapy za pomoc� tr�jk�t�w 
  // (po 4 tr�jk�ty na ka�de pole):
  enum tr{ABC=0,ADB=1,BDE=2,CBE=3};       
  float x_begin = -field_size*number_of_columns/2,     // wsp�rz�dne lewego g�rnego kra�ca envu
        z_begin = -field_size*number_of_rows/2;        
  Vector3 A,B,C,D,E,N;      
  glNewList(EnvironmentMap,GL_COMPILE);
  glBegin(GL_TRIANGLES);
    
    for (long w=0;w<number_of_rows;w++) 
      for (long k=0;k<number_of_columns;k++) 
      {
          A = Vector3(x_begin + k*field_size, height_map[w*2][k], z_begin + w*field_size);
          B = Vector3(x_begin + (k+0.5)*field_size, height_map[w*2+1][k], z_begin + (w+0.5)*field_size);            
          C = Vector3(x_begin + (k+1)*field_size, height_map[w*2][k+1], z_begin + w*field_size); 
          D = Vector3(x_begin + k*field_size, height_map[(w+1)*2][k], z_begin + (w+1)*field_size);       
          E = Vector3(x_begin + (k+1)*field_size, height_map[(w+1)*2][k+1], z_begin + (w+1)*field_size); 
          // tworz� tr�jk�t ABC w g�rnej cz�ci kwadratu: 
          //  A o_________o C
          //    |.       .|
          //    |  .   .  | 
          //    |    o B  | 
          //    |  .   .  |
          //    |._______.|
          //  D o         o E
          
          Vector3 AB = B-A;
          Vector3 BC = C-B;
          N = (AB*BC).znorm();          
          glNormal3f( N.x, N.y, N.z);
		  glVertex3f( A.x, A.y, A.z);
		  glVertex3f( B.x, B.y, B.z);
          glVertex3f( C.x, C.y, C.z);
          d[w][k][ABC] = -(B^N);          // dodatkowo wyznaczam wyraz wolny z r�wnania plaszyzny tr�jk�ta
          Norm[w][k][ABC] = N;          // dodatkowo zapisuj� normaln� do p�aszczyzny tr�jk�ta
          // tr�jk�t ADB:
          Vector3 AD = D-A;
          N = (AD*AB).znorm();          
          glNormal3f( N.x, N.y, N.z);
		  glVertex3f( A.x, A.y, A.z);
		  glVertex3f( D.x, D.y, D.z);
		  glVertex3f( B.x, B.y, B.z);
		  d[w][k][ADB] = -(B^N);       
          Norm[w][k][ADB] = N;
		  // tr�jk�t BDE:
          Vector3 BD = D-B;
          Vector3 DE = E-D;
          N = (BD*DE).znorm();          
          glNormal3f( N.x, N.y, N.z);
		  glVertex3f( B.x, B.y, B.z);
          glVertex3f( D.x, D.y, D.z);     
          glVertex3f( E.x, E.y, E.z);  
          d[w][k][BDE] = -(B^N);        
          Norm[w][k][BDE] = N;  
          // tr�jk�t CBE:
          Vector3 CB = B-C;
          Vector3 BE = E-B;
          N = (CB*BE).znorm();          
          glNormal3f( N.x, N.y, N.z);
          glVertex3f( C.x, C.y, C.z);
		  glVertex3f( B.x, B.y, B.z);
          glVertex3f( E.x, E.y, E.z);      
          d[w][k][CBE] = -(B^N);        
          Norm[w][k][CBE] = N;
      }		

  glEnd();
  glEndList(); 
                 
}

// wczytanie powierzchni terenu (mapy wysoko�ci) oraz przedmiot�w  
int Environment::ReadMap(char filename[128])
{
	int mode_reading_things = 0, mode_reading_map = 0, mode_reading_row = 0,
		nr_of_row_point = -1, nr_of_column_point = -1;   // liczby wierszy i kolumn punkt�w 
	height_map = NULL;
   
	this->number_of_rows = this->number_of_columns = 0;  // liczby wierszy i kolumn czw�rek tr�jk�t�w

	FILE *pl = fopen(filename, "r");

	if (pl)
	{
		char line[1024], writing[128];
		long long_number;
		Vector3 wektor;
		quaternion kw;
		float float_number;
		while (fgets(line, 1024, pl))
		{
			sscanf(line, "%s", &writing);
			
			if (strcmp(writing, "<mapa>") == 0)
			{
				mode_reading_map = 1;
			}
			
			if (mode_reading_map)
			{
				if (strcmp(writing, "<liczba_wierszy") == 0)
				{
					sscanf(line, "%s %d ", &writing, &long_number);
					this->number_of_rows = long_number;
				}
				else if (strcmp(writing, "<liczba_kolumn") == 0)
				{
					sscanf(line, "%s %d ", &writing, &long_number);
					this->number_of_columns = long_number;
				}
				else if (strcmp(writing, "<wiersz_punktow") == 0)
				{
					mode_reading_row = 1;
					sscanf(line, "%s %d ", &writing, &long_number);
					nr_of_row_point = long_number;
					nr_of_column_point = 0;
				}
				else if (strcmp(writing, "</mapa>") == 0)
				{
					mode_reading_map = 0;
				}

				if (mode_reading_row)
				{
					if (strcmp(writing, "<w") == 0)
					{
						sscanf(line, "%s %f ", &writing, &float_number);
						height_map[nr_of_row_point][nr_of_column_point] = float_number;
						nr_of_column_point++;
					}
					else if (strcmp(writing, "</wiersz_punktow>") == 0)
					{
						mode_reading_row = 0;
					}
				}

			} // tryb odczytu mapy wierzcho�k�w

			// pami�� dla mapy terenu:
			if ((this->number_of_rows > 0) && (this->number_of_columns > 0) && (height_map == NULL))
			{
				height_map = new float*[number_of_rows * 2 + 1];
				for (long i = 0; i<number_of_rows * 2 + 1; i++) {
					height_map[i] = new float[number_of_columns + 1];
					for (long j = 0; j<number_of_columns + 1; j++) height_map[i][j] = 0;
				}
			}

		}
		fclose(pl);
	}
	else return 0;

	return 1;
}


void Environment::Draw()
{
  glCallList(EnvironmentMap);                  
}

   
