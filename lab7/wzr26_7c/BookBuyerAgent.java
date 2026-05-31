/*  Klasa agenta kupującego książki w imieniu właściciela
 *
 *  parametry linii uruchomienia:
 *  -agents seller1:BookSellerAgent();seller2:BookSellerAgent();buyer1:BookBuyerAgent(Zamek) -gui
 */
        
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
     
      //doWait(6000);   // Oczekiwanie na uruchomienie agentów sprzedających

      System.out.println("Witam! Agent-kupiec "+getAID().getName()+" (wersja c <2025/26>) jest gotów do kupowania!");

      Object[] args = getArguments();  // lista argumentów wejściowych (tytuł książki)

      if (args != null && args.length > 0)   // jeśli podano tytuł książki
      {
        targetBookTitle = (String) args[0];
        System.out.println("Zamierzam kupić książkę zatytułowaną "+targetBookTitle);

        addBehaviour(new RequestPerformer());  // dodanie głównej klasy zachowań - kod znajduje się poniżej
       
      }
      else
      {
        // Jeśli nie przekazano poprzez argument tytułu książki, agent kończy działanie:
        System.out.println("Proszę podać tytuł lektury w argumentach wejściowych agenta kupującego!");
        doDelete();
      }
    }
    // Metoda realizująca zakończenie pracy agenta:
    protected void takeDown()
    {
      System.out.println("Agent-kupiec "+getAID().getName()+" kończy.");
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
      private int lastSellerOffer = -1;
      private int lastBuyerOffer = -1;
      private int negotiationRound = 0;
      private final int maxNegotiationRounds = 8;

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
            cfp.addReceiver(sellerAgents[i]);                // dodanie adresata
          }
          cfp.setContent(targetBookTitle);                   // wpisanie zawartości - tytułu książki
          cfp.setConversationId("handel_ksiazkami");         // wpisanie specjalnego identyfikatora korespondencji
          cfp.setReplyWith("cfp"+System.currentTimeMillis()); // dodatkowa unikatowa wartość, żeby w razie odpowiedzi zidentyfikować adresatów
          myAgent.send(cfp);                           // wysłanie wiadomości

          // Utworzenie szablonu do odbioru ofert sprzedaży tylko od wskazanych sprzedawców:
          mt = MessageTemplate.and(MessageTemplate.MatchConversationId("handel_ksiazkami"),
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
              if (bestSeller == null || price < bestPrice)       // jeśli jest to najlepsza oferta
              {
                bestPrice = price;
                bestSeller = reply.getSender();
              }
            }
            repliesCnt++;                                        // liczba ofert
            if (repliesCnt >= sellerAgents.length)               // jeśli liczba ofert co najmniej liczbie sprzedawców
            {
              lastSellerOffer = bestPrice;
              lastBuyerOffer = (int) Math.round(bestPrice * 0.75);
              System.out.println("Kupiec wybiera sprzedawcę: "+bestSeller.getLocalName()+" z ceną "
                + bestPrice +"; pierwsza kontroferta ("+lastBuyerOffer+")");
              step = 2;
            }
          }
          else
          {
            block();
          }
          break;
        case 2:      // wysłanie pierwszej kontroferty (-25%) do sprzedawcy z najlepszą ofertą
          if (bestSeller == null) {
            step = 5;
            break;
          }
          ACLMessage counter = new ACLMessage(ACLMessage.PROPOSE);
          counter.addReceiver(bestSeller);
          counter.setContent(targetBookTitle+"|"+String.valueOf(lastBuyerOffer));
          counter.setConversationId("handel_ksiazkami");
          counter.setReplyWith("counter"+System.currentTimeMillis());
          myAgent.send(counter);
          System.out.println("Kupiec wysyła kontrofertę: "+lastBuyerOffer);
          mt = MessageTemplate.and(MessageTemplate.MatchConversationId("handel_ksiazkami"),
                                   MessageTemplate.MatchInReplyTo(counter.getReplyWith()));
          step = 3;
          break;
        case 3:      // odbiór propozycji od sprzedawcy po kontrofertach
          reply = myAgent.receive(mt);
          if (reply != null)
          {
            if (reply.getPerformative() == ACLMessage.PROPOSE)
            {
              int sellerPrice = Integer.parseInt(reply.getContent());
              lastSellerOffer = sellerPrice;
              System.out.println("Kupiec otrzymał ofertę sprzedawcy: "+sellerPrice);
              if (sellerPrice <= lastBuyerOffer)
              {
                ACLMessage order = new ACLMessage(ACLMessage.ACCEPT_PROPOSAL);
                order.addReceiver(bestSeller);
                order.setContent(targetBookTitle);
                order.setConversationId("handel_ksiazkami");
                order.setReplyWith("order"+System.currentTimeMillis());
                myAgent.send(order);
                mt = MessageTemplate.and(MessageTemplate.MatchConversationId("handel_ksiazkami"),
                                         MessageTemplate.MatchInReplyTo(order.getReplyWith()));
                bestPrice = sellerPrice;
                step = 4;
              }
              else
              {
                negotiationRound++;
                if (negotiationRound >= maxNegotiationRounds)
                {
                  System.out.println("Nie udało się uzgodnić ceny w czasie negocjacji.");
                  myAgent.doDelete();
                  step = 5;
                }
                else
                {
                  lastBuyerOffer = (lastSellerOffer + lastBuyerOffer) / 2;
                  System.out.println("Kupiec wyznacza średnią i kontrofertę: "+lastBuyerOffer);
                  ACLMessage nextCounter = new ACLMessage(ACLMessage.PROPOSE);
                  nextCounter.addReceiver(bestSeller);
                  nextCounter.setContent(targetBookTitle+"|"+String.valueOf(lastBuyerOffer));
                  nextCounter.setConversationId("handel_ksiazkami");
                  nextCounter.setReplyWith("counter"+System.currentTimeMillis());
                  myAgent.send(nextCounter);
                  System.out.println("Kupiec wysyła kontrofertę: "+lastBuyerOffer);
                  mt = MessageTemplate.and(MessageTemplate.MatchConversationId("handel_ksiazkami"),
                                           MessageTemplate.MatchInReplyTo(nextCounter.getReplyWith()));
                }
              }
            }
          }
          else
          {
            block();
          }
          break;
        case 4:      // odbiór odpowiedzi na zamównienie
          reply = myAgent.receive(mt);
          if (reply != null)
          {
            if (reply.getPerformative() == ACLMessage.INFORM)
            {
              System.out.println("Tytuł "+targetBookTitle+" zamówiony!");
              System.out.println("Po cenie: "+bestPrice);
              myAgent.doDelete();
            }
            step = 5;
          }
          else
          {
            block();
          }
          break;
        }  // switch
      } // action

      public boolean done() {
        return ((step == 2 && bestSeller == null) || step == 5);
      }
    } // Koniec wewnętrznej klasy RequestPerformer
}
