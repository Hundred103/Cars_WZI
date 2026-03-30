/************************************************************
					 Grafika OpenGL
*************************************************************/
#include <windows.h>
#include <time.h>
#include <gl\gl.h>
#include <gl\glu.h>
#include <iterator> 
#include <map>

using namespace std;

#include "graphics.h"
#include "objects.h"

ViewParams view_parameters;

extern MovableObject *my_vehicle;               // obiekt przypisany do tej aplikacji
extern map<int, MovableObject*> other_vehicles;
extern CRITICAL_SECTION m_cs;

extern Terrain terrain;
extern long time_of_day;

int g_GLPixelIndex = 0;
HGLRC g_hGLContext = NULL;
unsigned int font_base;
extern long time_start;                 // czas od uruchomienia potrzebny np. do obliczenia po³o¿enia s³oñca
extern HWND window_handle;

extern void CreateDisplayLists();		// definiujemy listy tworz¹ce labirynt
extern void DrawGlobalCoordAxes();


int GraphicsInitialization(HDC g_context)
{

	if (SetWindowPixelFormat(g_context) == FALSE)
		return FALSE;

	if (CreateViewGLContext(g_context) == FALSE)
		return 0;
	BuildFont(g_context);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);


	CreateDisplayLists();		// definiujemy listy tworz¹ce ró¿ne elementy sceny
	terrain.DrawInitialisation();

	// pocz¹tkowe ustawienia widoku:
	// Parametry widoku:
	view_parameters.cam_direct_1 = Vector3(10, -3, -15);   // kierunek patrzenia
	view_parameters.cam_pos_1 = Vector3(-35, 6, 10);         // po³o¿enie kamery
	view_parameters.cam_vertical_1 = Vector3(0, 1, 0);           // kierunek pionu kamery        
	view_parameters.cam_direct_2 = Vector3(0, -1, 0.02);   // to samo dla widoku z góry
	view_parameters.cam_pos_2 = Vector3(0, 100, 0);
	view_parameters.cam_vertical_2 = Vector3(0, 0, -1);
	view_parameters.cam_direct = view_parameters.cam_direct_1;
	view_parameters.cam_pos = view_parameters.cam_pos_1;
	view_parameters.cam_vertical = view_parameters.cam_vertical_1;
	view_parameters.tracking = 1;                             // tryb œledzenia obiektu przez kamerê
	view_parameters.top_view = 0;                          // tryb widoku z góry
	view_parameters.cam_distance = 40.0;                          // cam_distance widoku z kamery
	view_parameters.cam_angle = 0;                            // obrót kamery góra-dó³
	view_parameters.cam_distance_1 = view_parameters.cam_distance;
	view_parameters.cam_angle_1 = view_parameters.cam_angle;
	view_parameters.cam_distance_2 = view_parameters.cam_distance;
	view_parameters.cam_angle_2 = view_parameters.cam_angle;
	view_parameters.cam_distance_3 = view_parameters.cam_distance;
	view_parameters.cam_angle_3 = view_parameters.cam_angle;
	view_parameters.zoom = 0.7;
	view_parameters.network_shadow_view = false;
}


void DrawGLScene()
{
	GLfloat OwnVehicleColor[] = { 0.4f, 0.0f, 0.8f, 0.5f };
	GLfloat AlienVehiclesColor[] = { 0.4f, 0.8f, 0.25f, 0.7f };
	GLfloat RedSurface[] = { 0.9f, 0.4f, 0.2f, 0.6f };
	
	GLfloat YellowSurface[] = { 0.75f, 0.75f, 0.0f, 1.0f };
	

	GLfloat LightAmbient[] = { 0.1f, 0.1f, 0.1f, 0.1f };
	GLfloat LightDiffuse[] = { 0.4f, 0.7f, 0.7f, 0.7f };
	GLfloat LightPosition[] = { 5.0f, 5.0f, 5.0f, 0.0f };

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glLightfv(GL_LIGHT0, GL_AMBIENT, LightAmbient);		//1 sk³adowa: œwiat³o otaczaj¹ce (bezkierunkowe)
	glLightfv(GL_LIGHT0, GL_DIFFUSE, LightDiffuse);		//2 sk³adowa: œwiat³o rozproszone (kierunkowe)
	glLightfv(GL_LIGHT0, GL_POSITION, LightPosition);
	glEnable(GL_LIGHT0);

	glPushMatrix();
	//glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, BlueSurface);

	

	Vector3 direction_k, vertical_k, position_k;
	if (view_parameters.tracking)
	{
		direction_k = my_vehicle->state.qOrient.rotate_vector(Vector3(1, 0, 0));
		vertical_k = my_vehicle->state.qOrient.rotate_vector(Vector3(0, 1, 0));
		Vector3 v_cam_right = my_vehicle->state.qOrient.rotate_vector(Vector3(0, 0, 1));
		if (view_parameters.network_shadow_view)
		{
			direction_k = other_vehicles[my_vehicle->iID]->state.qOrient.rotate_vector(Vector3(1, 0, 0));
			vertical_k = other_vehicles[my_vehicle->iID]->state.qOrient.rotate_vector(Vector3(0, 1, 0));
			Vector3 v_cam_right = other_vehicles[my_vehicle->iID]->state.qOrient.rotate_vector(Vector3(0, 0, 1));
		}

		vertical_k = vertical_k.rotation(view_parameters.cam_angle, v_cam_right.x, v_cam_right.y, v_cam_right.z);
		direction_k = direction_k.rotation(view_parameters.cam_angle, v_cam_right.x, v_cam_right.y, v_cam_right.z);
		position_k = my_vehicle->state.vPos - direction_k * my_vehicle->length * 0 +
			vertical_k.znorm() * my_vehicle->height * 5;
		if (view_parameters.network_shadow_view)
		{
			position_k = other_vehicles[my_vehicle->iID]->state.vPos - direction_k * other_vehicles[my_vehicle->iID]->length * 0 +
				vertical_k.znorm() * other_vehicles[my_vehicle->iID]->height * 5;
		}

		view_parameters.cam_vertical = vertical_k;
		view_parameters.cam_direct = direction_k;
		view_parameters.cam_pos = position_k;
	}
	else
	{
		vertical_k = view_parameters.cam_vertical;
		direction_k = view_parameters.cam_direct;
		position_k = view_parameters.cam_pos;
		Vector3 v_cam_right = (direction_k*vertical_k).znorm();
		vertical_k = vertical_k.rotation(view_parameters.cam_angle / 20, v_cam_right.x, v_cam_right.y, v_cam_right.z);
		direction_k = direction_k.rotation(view_parameters.cam_angle / 20, v_cam_right.x, v_cam_right.y, v_cam_right.z);
	}

	// Ustawianie widoku sceny    
	gluLookAt(position_k.x - view_parameters.cam_distance*direction_k.x,
		position_k.y - view_parameters.cam_distance*direction_k.y, position_k.z - view_parameters.cam_distance*direction_k.z,
		position_k.x + direction_k.x, position_k.y + direction_k.y, position_k.z + direction_k.z,
		vertical_k.x, vertical_k.y, vertical_k.z);

	//glRasterPos2f(0.30,-0.27);
	//glPrint("my_vehicle->iID = %d",my_vehicle->iID ); 

	DrawGlobalCoordAxes();

	// s³oñce + t³o:
	int R = 70000;                // promieñ obiegu
	long x = (clock() - time_start) % (time_of_day*CLOCKS_PER_SEC);
	float angle = (float)x / (time_of_day*CLOCKS_PER_SEC) * 2 * 3.1416;
	//char lan[128];
	//sprintf(lan,"angle = %f\n", angle);
	//SetWindowText(window_handle, lan);

	float cos_abs = fabs(cos(angle));
	float cos_sq = cos_abs*cos_abs, sin_sq = sin(angle)*sin(angle);
	float sin_angminpi = fabs(sin(angle/2 - 3.1416/2));                   // 0 gdy s³oñce na antypodach, 1 w po³udnie
	float sin_angminpi_sqr = sin_angminpi*sin_angminpi;
	float cos_ang_zachod = fabs(cos((angle - 1.5)*3));                    // 1 w momencie wschodu lub zachodu  
	glClearColor(0.65*cos_abs*sin_angminpi_sqr + 0.25*sin_sq*cos_ang_zachod, 0.75*cos_abs*sin_angminpi_sqr, 4.0*cos_abs*sin_angminpi_sqr, 1.0);  // ustawienie t³a
	GLfloat SunColor[] = { 8.0*cos_sq, 8.0 * cos_sq*cos_sq*sin_angminpi, 4.0 * cos_sq*cos_sq*sin_angminpi, 1.0f };
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, SunColor);
	glPushMatrix();
	glTranslatef(R*cos(angle), R*(cos(angle)*0.5 - 0.2), R*sin(angle));   // ustawienie s³oñca
	//glTranslatef(R*cos(angle), 5000, R*sin(angle));
	GLUquadricObj *Qsph = gluNewQuadric();
	gluSphere(Qsph, 600.0 + 5000 * sin_sq*sin_sq, 27, 27);
	gluDeleteQuadric(Qsph);
	glPopMatrix();

	GLfloat GroundSurface[] = { 0.6*(0.4 + 0.6*sin_angminpi), 0.5*(0.2 + 0.8*sin_angminpi), 0.3*(0.1 + 0.9*sin_angminpi), 1.0f };

	//glPushMatrix();
	for (int w = -1; w < 2; w++)
		for (int k = -1; k < 2; k++)
		{
			glPushMatrix();

			glTranslatef(terrain.number_of_columns*terrain.field_size*k, 0, terrain.number_of_rows*terrain.field_size*w);

			glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, OwnVehicleColor);
			glEnable(GL_BLEND);

			my_vehicle->DrawObject();

			// Lock the Critical section
			EnterCriticalSection(&m_cs);
			for (map<int, MovableObject*>::iterator it = other_vehicles.begin(); it != other_vehicles.end(); ++it)
			{
				if (it->second)
				{
					if (it->second->iID == my_vehicle->iID)
						glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, RedSurface);
					else
						glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, AlienVehiclesColor);
					it->second->DrawObject();
				}
			}
			//Release the Critical section
			LeaveCriticalSection(&m_cs);

			glDisable(GL_BLEND);
			glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, GroundSurface);

			terrain.Draw();
			glPopMatrix();
		}



	glPopMatrix();

	glFlush();

}

void WindowResize(int cx, int cy)
{
	GLsizei width, height;
	GLdouble aspect;
	width = cx;
	height = cy;

	if (cy == 0)
		aspect = (GLdouble)width;
	else
		aspect = (GLdouble)width / (GLdouble)height;

	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(35 * view_parameters.zoom, aspect, 1, 100000.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDrawBuffer(GL_BACK);

	glEnable(GL_LIGHTING);

	glEnable(GL_DEPTH_TEST);

}


void EndOfGraphics()
{
	if (wglGetCurrentContext() != NULL)
	{
		// dezaktualizacja contextu renderuj¹cego
		wglMakeCurrent(NULL, NULL);
	}
	if (g_hGLContext != NULL)
	{
		wglDeleteContext(g_hGLContext);
		g_hGLContext = NULL;
	}
	glDeleteLists(font_base, 96);
}

BOOL SetWindowPixelFormat(HDC hDC)
{
	PIXELFORMATDESCRIPTOR pixelDesc;

	pixelDesc.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pixelDesc.nVersion = 1;
	pixelDesc.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_STEREO_DONTCARE;
	pixelDesc.iPixelType = PFD_TYPE_RGBA;
	pixelDesc.cColorBits = 32;
	pixelDesc.cRedBits = 8;
	pixelDesc.cRedShift = 16;
	pixelDesc.cGreenBits = 8;
	pixelDesc.cGreenShift = 8;
	pixelDesc.cBlueBits = 8;
	pixelDesc.cBlueShift = 0;
	pixelDesc.cAlphaBits = 0;
	pixelDesc.cAlphaShift = 0;
	pixelDesc.cAccumBits = 64;
	pixelDesc.cAccumRedBits = 16;
	pixelDesc.cAccumGreenBits = 16;
	pixelDesc.cAccumBlueBits = 16;
	pixelDesc.cAccumAlphaBits = 0;
	pixelDesc.cDepthBits = 32;
	pixelDesc.cStencilBits = 8;
	pixelDesc.cAuxBuffers = 0;
	pixelDesc.iLayerType = PFD_MAIN_PLANE;
	pixelDesc.bReserved = 0;
	pixelDesc.dwLayerMask = 0;
	pixelDesc.dwVisibleMask = 0;
	pixelDesc.dwDamageMask = 0;
	g_GLPixelIndex = ChoosePixelFormat(hDC, &pixelDesc);

	if (g_GLPixelIndex == 0)
	{
		g_GLPixelIndex = 1;

		if (DescribePixelFormat(hDC, g_GLPixelIndex, sizeof(PIXELFORMATDESCRIPTOR), &pixelDesc) == 0)
		{
			return FALSE;
		}
	}

	if (SetPixelFormat(hDC, g_GLPixelIndex, &pixelDesc) == FALSE)
	{
		return FALSE;
	}

	return TRUE;
}
BOOL CreateViewGLContext(HDC hDC)
{
	g_hGLContext = wglCreateContext(hDC);

	if (g_hGLContext == NULL)
	{
		return FALSE;
	}

	if (wglMakeCurrent(hDC, g_hGLContext) == FALSE)
	{
		return FALSE;
	}

	return TRUE;
}

GLvoid BuildFont(HDC hDC)								// Build Our Bitmap Font
{
	HFONT	font;										// Windows Font ID
	HFONT	oldfont;									// Used For Good House Keeping

	font_base = glGenLists(96);								// Storage For 96 Characters

	font = CreateFont(-16,							// Height Of Font
		0,								// Width Of Font
		0,								// Angle Of Escapement
		0,								// Orientation Angle
		FW_NORMAL,						// Font Weight
		TRUE,							// Italic
		FALSE,							// Underline
		FALSE,							// Strikeout
		ANSI_CHARSET,					// Character Set Identifier
		OUT_TT_PRECIS,					// Output Precision
		CLIP_DEFAULT_PRECIS,			// Clipping Precision
		ANTIALIASED_QUALITY,			// Output Quality
		FF_DONTCARE | DEFAULT_PITCH,		// Family And Pitch
		"Courier New");					// Font Name

	oldfont = (HFONT)SelectObject(hDC, font);           // Selects The Font We Want
	wglUseFontBitmaps(hDC, 32, 96, font_base);				// Builds 96 Characters Starting At Character 32
	SelectObject(hDC, oldfont);							// Selects The Font We Want
	DeleteObject(font);									// Delete The Font
}

// Napisy w OpenGL
GLvoid glPrint(const char *fmt, ...)	// Custom GL "Print" Routine
{
	char		text[256];	// Holds Our String
	va_list		ap;		// Pointer To List Of Arguments

	if (fmt == NULL)		// If There's No Text
		return;			// Do Nothing

	va_start(ap, fmt);		// Parses The String For Variables
	vsprintf(text, fmt, ap);	// And Converts Symbols To Actual Numbers
	va_end(ap);			// Results Are Stored In Text

	glPushAttrib(GL_LIST_BIT);	// Pushes The Display List Bits
	glListBase(font_base - 32);		// Sets The Base Character to 32
	glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);	// Draws The Display List Text
	glPopAttrib();			// Pops The Display List Bits
}


void CreateDisplayLists()
{
	glNewList(Wall1, GL_COMPILE);	// GL_COMPILE - lista jest kompilowana, ale nie wykonywana

	glBegin(GL_QUADS);		// inne opcje: GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_LINE_LOOP
	// GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUAD_STRIP, GL_POLYGON
	glNormal3f(-1.0, 0.0, 0.0);
	glVertex3f(-1.0, -1.0, 1.0);
	glVertex3f(-1.0, 1.0, 1.0);
	glVertex3f(-1.0, 1.0, -1.0);
	glVertex3f(-1.0, -1.0, -1.0);
	glEnd();
	glEndList();

	glNewList(Wall2, GL_COMPILE);
	glBegin(GL_QUADS);
	glNormal3f(1.0, 0.0, 0.0);
	glVertex3f(1.0, -1.0, 1.0);
	glVertex3f(1.0, 1.0, 1.0);
	glVertex3f(1.0, 1.0, -1.0);
	glVertex3f(1.0, -1.0, -1.0);
	glEnd();
	glEndList();

	glNewList(Auto, GL_COMPILE);
	glBegin(GL_QUADS);
	// przod
	glNormal3f(0.0, 0.0, 1.0);

	glVertex3f(0, 0, 1);
	glVertex3f(0, 1, 1);
	glVertex3f(0.7, 1, 1);
	glVertex3f(0.7, 0, 1);

	glVertex3f(0.7, 0, 1);
	glVertex3f(0.7, 0.5, 1);
	glVertex3f(1.0, 0.5, 1);
	glVertex3f(1.0, 0, 1);
	// tyl
	glNormal3f(0.0, 0.0, -1.0);
	glVertex3f(0, 0, 0);
	glVertex3f(0.7, 0, 0);
	glVertex3f(0.7, 1, 0);
	glVertex3f(0, 1, 0);

	glVertex3f(0.7, 0, 0);
	glVertex3f(1.0, 0, 0);
	glVertex3f(1.0, 0.5, 0);
	glVertex3f(0.7, 0.5, 0);
	// gora
	glNormal3f(0.0, 1.0, 0.0);
	glVertex3f(0, 1, 0);
	glVertex3f(0, 1, 1);
	glVertex3f(0.7, 1, 1);
	glVertex3f(0.7, 1, 0);

	glVertex3f(0.7, 0.5, 0);
	glVertex3f(0.7, 0.5, 1);
	glVertex3f(1.0, 0.5, 1);
	glVertex3f(1.0, 0.5, 0);
	// dol
	glNormal3f(0.0, -1.0, 0.0);
	glVertex3f(0, 0, 0);
	glVertex3f(1, 0, 0);
	glVertex3f(1, 0, 1);
	glVertex3f(0, 0, 1);
	// prawo
	glNormal3f(1.0, 0.0, 0.0);
	glVertex3f(0.7, 0.5, 0);
	glVertex3f(0.7, 0.5, 1);
	glVertex3f(0.7, 1, 1);
	glVertex3f(0.7, 1, 0);

	glVertex3f(1.0, 0.0, 0);
	glVertex3f(1.0, 0.0, 1);
	glVertex3f(1.0, 0.5, 1);
	glVertex3f(1.0, 0.5, 0);
	// lewo
	glNormal3f(-1.0, 0.0, 0.0);
	glVertex3f(0, 0, 0);
	glVertex3f(0, 1, 0);
	glVertex3f(0, 1, 1);
	glVertex3f(0, 0, 1);

	glEnd();
	glEndList();

}


void DrawGlobalCoordAxes(void)
{

	glColor3f(1, 0, 0);
	glBegin(GL_LINES);
	glVertex3f(0, 0, 0);
	glVertex3f(2, 0, 0);
	glVertex3f(2, -0.25, 0.25);
	glVertex3f(2, 0.25, -0.25);
	glVertex3f(2, -0.25, -0.25);
	glVertex3f(2, 0.25, 0.25);

	glEnd();
	glColor3f(0, 1, 0);
	glBegin(GL_LINES);
	glVertex3f(0, 0, 0);
	glVertex3f(0, 2, 0);
	glVertex3f(0, 2, 0);
	glVertex3f(0.25, 2, 0);
	glVertex3f(0, 2, 0);
	glVertex3f(-0.25, 2, 0.25);
	glVertex3f(0, 2, 0);
	glVertex3f(-0.25, 2, -0.25);

	glEnd();
	glColor3f(0, 0, 1);
	glBegin(GL_LINES);
	glVertex3f(0, 0, 0);
	glVertex3f(0, 0, 2);
	glVertex3f(-0.25, -0.25, 2);
	glVertex3f(0.25, 0.25, 2);
	glVertex3f(-0.25, -0.25, 2);
	glVertex3f(0.25, -0.25, 2);
	glVertex3f(-0.25, 0.25, 2);
	glVertex3f(0.25, 0.25, 2);

	glEnd();

	glColor3f(1, 1, 1);
}

