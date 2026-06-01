// Przykładowa lista parametrów uruchomienia:
// -agents seller1:BookSellerAgent();seller2:BookSellerAgent();buyer1:BookBuyerAgent(Zamek) -gui
import jade.core.Agent;
import jade.core.AID;
import jade.core.behaviours.*;
import jade.lang.acl.*;
import java.util.*;

// Przykładowa klasa zachowania:
class MyOwnBehaviour extends Behaviour
{
  protected MyOwnBehaviour()
  {
  }
  public void action()
  {
  }
  public boolean done() {
    return false;
  }
}

public class BookBuyerAgent extends Agent {

    private String targetBookTitle;    // tytuł kupowanej książki przekazywany poprzez argument wejściowy
    // lista znanych agentów sprzedających książki (w przypadku użycia żółtej księgi - usługi katalogowej, sprzedawcy
    // mogą być dołączani do listy dynamicznie!
    private AID[] sellerAgents = {
      new AID("seller1", AID.ISLOCALNAME),
      new AID("seller2", AID.ISLOCALNAME)};
    
    // Inicjalizacja klasy agenta:
    protected void setup()
    {
     
      //doWait(4000);   // Oczekiwanie na uruchomienie agentów sprzedających

      System.out.println("Witam! Agent-kupiec "+getAID().getName()+" (wersja g, 2025/26) jest gotów do handlu!");

      Object[] args = getArguments();  // lista argumentów wejściowych (tytuł książki)

      if (args != null && args.length > 0)   // jeśli podano tytuł książki
      {
        targetBookTitle = (String) args[0];
        System.out.println("Mam zamiar kupić książkę zatytułowaną "+targetBookTitle);

        addBehaviour(new RequestPerformer());  // dodanie głównej klasy zachowań - kod znajduje się poniżej
       
      }
      else
      {
        // Jeśli nie przekazano poprzez argument tytułu książki, agent kończy działanie:
        System.out.println("Należy podać tytuł książki w argumentach wejściowych kupca!");
        doDelete();
      }
    }
    // Metoda realizująca zakończenie pracy agenta:
    protected void takeDown()
    {
      System.out.println("Agent-kupiec "+getAID().getName()+" wylogował się.");
    }

    /**
    Inner class RequestPerformer.
    This is the behaviour used by Book-buyer agents to request seller
    agents the target book.
    */
    private class RequestPerformer extends Behaviour
    {
       
      private AID bestSeller;     // agent sprzedający z najkorzystniejszą ofertą
      private int bestPrice;      // najlepsza cena
      private int repliesCnt = 0; // liczba odpowiedzi od agentów
      private MessageTemplate mt; // szablon odpowiedzi
      private int step = 0;       // krok
      private int lastSellerPrice = -1;
      private int lastBuyerPrice = -1;

      public void action()
      {
        switch (step) {
        case 0:      // wysłanie oferty kupna
          System.out.print(" Oferta kupna (CFP) jest wysyłana do: ");
          for (int i = 0; i < sellerAgents.length; ++i)
          {
            System.out.print(sellerAgents[i]+ " ");
          }
          System.out.println();

          // Tworzenie wiadomości CFP do wszystkich sprzedawców:
          ACLMessage cfp = new ACLMessage(ACLMessage.CFP);
          for (int i = 0; i < sellerAgents.length; ++i)
          {
            cfp.addReceiver(sellerAgents[i]);          // dodanie adresate
          }
          cfp.setContent(targetBookTitle);             // wpisanie zawartości - tytułu książki
          cfp.setConversationId("book-trade");         // wpisanie specjalnego identyfikatora korespondencji
          cfp.setReplyWith("cfp"+System.currentTimeMillis()); // dodatkowa unikatowa wartość, żeby w razie odpowiedzi zidentyfikować adresatów
          myAgent.send(cfp);                           // wysłanie wiadomości

          // Utworzenie szablonu do odbioru ofert sprzedaży tylko od wskazanych sprzedawców:
          mt = MessageTemplate.and(MessageTemplate.MatchConversationId("book-trade"),
                                   MessageTemplate.MatchInReplyTo(cfp.getReplyWith()));
          step = 1;     // przejście do kolejnego kroku
          break;
        case 1:      // odbiór ofert sprzedaży/odmowy od agentów-sprzedawców
          ACLMessage reply = myAgent.receive(mt);      // odbiór odpowiedzi
          if (reply != null)
          {
            if (reply.getPerformative() == ACLMessage.PROPOSE)   // jeśli wiadomość jest typu PROPOSE
            {
              int price = Integer.parseInt(reply.getContent());  // cena książki
              System.out.println("Agent-kupiec otrzymał ofertę " + price + " od " + reply.getSender().getName());
              if (bestSeller == null || price < bestPrice)       // jeśli jest to najlepsza oferta
              {
                bestPrice = price;
                bestSeller = reply.getSender();
              }
            }
            repliesCnt++;                                        // liczba ofert
            if (repliesCnt >= sellerAgents.length)               // jeśli liczba ofert co najmniej liczbie sprzedawców
            {
              if (bestSeller != null)
              {
                lastSellerPrice = bestPrice;
                lastBuyerPrice = (int)Math.round(bestPrice * 0.75);
                System.out.println("Agent-kupiec kontruje najlepszą ofertę (" + bestPrice + ") proponując: " + lastBuyerPrice);
                
                ACLMessage counter = new ACLMessage(ACLMessage.PROPOSE);
                counter.addReceiver(bestSeller);
                counter.setContent(String.valueOf(lastBuyerPrice));
                counter.setConversationId("book-trade");
                counter.setReplyWith("counter"+System.currentTimeMillis());
                myAgent.send(counter);
                
                mt = MessageTemplate.and(MessageTemplate.MatchConversationId("book-trade"),
                                         MessageTemplate.MatchSender(bestSeller));
                step = 2;
              }
              else
              {
                System.out.println("Brak dostępnych ofert od sprzedawców.");
                step = 4;
              }
            }
          }
          else
          {
            block();
          }
          break;
        case 2:      // negocjacje ceny ze sprzedawcą
          reply = myAgent.receive(mt);
          if (reply != null)
          {
            if (reply.getPerformative() == ACLMessage.PROPOSE)
            {
              try {
                int sellerPrice = Integer.parseInt(reply.getContent());
                lastSellerPrice = sellerPrice;
                System.out.println("Otrzymano nową ofertę od sprzedawcy: " + lastSellerPrice);
                
                if (Math.abs(lastSellerPrice - lastBuyerPrice) <= 2)
                {
                  System.out.println("Osiągnięto porozumienie! Wysyłam akceptację: " + lastSellerPrice);
                  ACLMessage order = new ACLMessage(ACLMessage.ACCEPT_PROPOSAL);
                  order.addReceiver(bestSeller);
                  order.setContent(targetBookTitle);
                  order.setConversationId("book-trade");
                  order.setReplyWith("order"+System.currentTimeMillis());
                  myAgent.send(order);
                  
                  mt = MessageTemplate.and(MessageTemplate.MatchConversationId("book-trade"),
                                           MessageTemplate.MatchInReplyTo(order.getReplyWith()));
                  step = 3;
                }
                else
                {
                  int nextBuyerPrice = (int)Math.round((3.0 * lastBuyerPrice + 2.0 * lastSellerPrice) / 5.0);
                  lastBuyerPrice = nextBuyerPrice;
                  System.out.println("Brak porozumienia. Wysyłam kontrofertę: " + lastBuyerPrice);
                  
                  ACLMessage counter = new ACLMessage(ACLMessage.PROPOSE);
                  counter.addReceiver(bestSeller);
                  counter.setContent(String.valueOf(lastBuyerPrice));
                  counter.setConversationId("book-trade");
                  counter.setReplyWith("counter"+System.currentTimeMillis());
                  myAgent.send(counter);
                }
              } catch (Exception e) {
                System.out.println("Błąd parsowania ceny sprzedawcy: " + reply.getContent());
                step = 4;
              }
            }
            else if (reply.getPerformative() == ACLMessage.REFUSE)
            {
              System.out.println("Sprzedawca odmówił dalszych negocjacji. Powód: " + reply.getContent());
              step = 4;
            }
            else
            {
              System.out.println("Nieoczekiwana wiadomość: " + ACLMessage.getPerformative(reply.getPerformative()));
            }
          }
          else
          {
            block();
          }
          break;
        case 3:      // odbiór odpowiedzi na zamównienie
          reply = myAgent.receive(mt);
          if (reply != null)
          {
            if (reply.getPerformative() == ACLMessage.INFORM)
            {
              System.out.println("Tytuł "+targetBookTitle+" został teraz zamówiony.");
              System.out.println("Cena = "+lastSellerPrice);
              myAgent.doDelete();
            }
            step = 4;
          }
          else
          {
            block();
          }
          break;
        }  // switch
      } // action

      public boolean done() {
        return (step == 4);
      }
    } // Koniec wewnętrznej klasy RequestPerformer
}
